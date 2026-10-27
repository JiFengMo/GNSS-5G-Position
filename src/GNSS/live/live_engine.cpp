#include "live_engine.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace b1live {
void Queue::push(Block b){
  std::lock_guard<std::mutex> lock(mutex_);if(closed_)return;
  if(b.iq.size()>capacity_){dropped_samples+=b.iq.size();++overruns;return;}
  if(samples_+b.iq.size()>capacity_){dropped_samples+=samples_;++overruns;blocks_.clear();samples_=0;}
  samples_+=b.iq.size();blocks_.push_back(std::move(b));
  if(samples_>peak_samples.load())peak_samples=samples_;
  cv_.notify_one();
}
bool Queue::pop(Block& b){std::unique_lock<std::mutex> lock(mutex_);cv_.wait(lock,[&]{return closed_||!blocks_.empty();});if(blocks_.empty())return false;b=std::move(blocks_.front());samples_-=b.iq.size();blocks_.pop_front();return true;}
void Queue::close(){std::lock_guard<std::mutex> lock(mutex_);closed_=true;cv_.notify_all();}
size_t Queue::samples(){std::lock_guard<std::mutex> lock(mutex_);return samples_;}
Options::Options(){bds_b1c_acquisition_defaults(&acquisition);bds_b1c_tracking_defaults(&tracking);}
Object tracking_json(const bds_b1c_tracking_epoch_t& e,double fs,uint64_t segment){
  Object r={{"prn",e.prn},{"epoch",counter(e.epoch)},{"generation",counter(e.generation)},
    {"absolute_sample",counter(e.absolute_sample)},{"sample_count",uint64_t(e.sample_count)},
    {"time_s",e.absolute_sample/fs},{"end_time_s",(e.absolute_sample+e.sample_count)/fs},
    {"integration_s",e.integration_s},{"segment",counter(segment)},{"valid",int(e.valid)},
    {"state",e.state==BDS_B1C_LOCKED?"locked":e.state==BDS_B1C_LOST?"lost":"pull_in"}};
#define FIELD(n) r[#n]=e.n;
  FIELD(carrier_hz) FIELD(doppler_hz) FIELD(code_rate_hz) FIELD(dll_error) FIELD(pll_error)
  FIELD(cn0_db_hz) FIELD(residual_frequency_hz) FIELD(rem_code_phase)
#undef FIELD
  const bds_b1c_correlation_t* corr[]={&e.data_early,&e.data_prompt,&e.data_late,&e.pilot_early,&e.pilot_prompt,&e.pilot_late};
  const char* names[]={"data_i_e","data_i_p","data_i_l","pilot_i_e","pilot_i_p","pilot_i_l"};
  for(int j=0;j<6;++j){std::string n=names[j];r[n]=corr[j]->i;n[n.find("_i_")+1]='q';r[n]=corr[j]->q;}
  return r;
}
Object frame_json(const bds_b1c_nav_frame_t& f,double fs,uint64_t segment){
  Object eph;
#define FIELD(n) eph[#n]=f.ephemeris.n;
  FIELD(PRN) FIELD(WN) FIELD(HOW) FIELD(SOH) FIELD(IODE) FIELD(IODC) FIELD(SatType) FIELD(PageID)
  FIELD(HS) FIELD(DIF) FIELD(SIF) FIELD(AIF) FIELD(SISMAI) FIELD(TOW) FIELD(t_oe) FIELD(t_oc)
  FIELD(deltaA) FIELD(ADot) FIELD(delta_n_0) FIELD(delta_n_0Dot) FIELD(M_0) FIELD(e) FIELD(omega)
  FIELD(omega_0) FIELD(i_0) FIELD(omegaDot) FIELD(i_0Dot) FIELD(C_is) FIELD(C_ic) FIELD(C_rs) FIELD(C_rc) FIELD(C_us) FIELD(C_uc)
  FIELD(a_0) FIELD(a_1) FIELD(a_2) FIELD(T_GDB2ap) FIELD(ISC_B1Cd) FIELD(T_GDB1Cp)
  FIELD(alpha1) FIELD(alpha2) FIELD(alpha3) FIELD(alpha4) FIELD(alpha5) FIELD(alpha6) FIELD(alpha7) FIELD(alpha8) FIELD(alpha9)
  FIELD(A_0UTC) FIELD(A_1UTC) FIELD(A_2UTC) FIELD(delta_t_LS) FIELD(t_ot) FIELD(WN_ot) FIELD(WN_LSF) FIELD(DN) FIELD(delta_t_LSF)
  FIELD(GNSS_ID) FIELD(WN_0BGTO) FIELD(t_0BGTO) FIELD(A_0BGTO) FIELD(A_1BGTO) FIELD(A_2BGTO)
#undef FIELD
  std::string bits;if(f.ephemeris.valid)for(uint8_t b:f.bits)bits+=char('0'+b);
  return {{"prn",f.prn},{"generation",counter(f.generation)},{"first_epoch",counter(f.first_epoch)},
    {"absolute_sample",counter(f.absolute_sample)},{"end_sample",counter(f.end_sample)},
    {"time_s",f.absolute_sample/fs},{"available_s",f.end_sample/fs},{"segment",counter(segment)},
    {"valid",int(f.ephemeris.valid)},{"status",bds_b1c_nav_status_name(f.status)},
    {"crc2_ok",int(f.crc2_ok)},{"crc3_ok",int(f.crc3_ok)},{"bits",bits},
    {"ephemeris",f.ephemeris.valid?Json(eph):Json()}};
}
struct Engine::Impl {
  struct Channel {
    Impl* owner=nullptr;int prn=0;bds_b1c_tracker_t* tracker=nullptr;bds_b1c_navigator_t* nav=nullptr;
    std::deque<bds_b1c_tracking_epoch_t> rows; bds_b1c_nav_frame_t anchor{};
    bool anchored=false,seen=false,reacquire=false;uint64_t generation=0,segment=0;
    std::vector<bds_b1c_tracking_epoch_t> pending_epochs;
    bool collect_failed=false;
    ~Channel(){bds_b1c_tracker_destroy(tracker);bds_b1c_navigator_destroy(nav);}
  };
  Engine* outer;Options o;Emit emit;Channel c[63];
  std::unique_ptr<bds_b1c_worker_pool_t,decltype(&bds_b1c_worker_pool_destroy)>
    tracking_pool{nullptr,bds_b1c_worker_pool_destroy};
  uint64_t expected=0,search_start=0,retry_at=0,survey_at=0;bool have_expected=false;
  std::vector<bds5g_cf32_t> search;std::vector<int> pending;bool initial=true;
  uint64_t next_fix=0,last_fix=0,index=0;double local=std::numeric_limits<double>::quiet_NaN();
  int base_week=0;bool have_xyz=false;double xyz[3]={0,0,0};
  Impl(Engine* e,Options opts,Emit cb):outer(e),o(std::move(opts)),emit(std::move(cb)){
    if(!std::isfinite(o.fs)||o.fs<2046000||o.fs>1e8||!std::isfinite(o.period_ms)||o.period_ms<=0||o.period_ms>10000||
       o.fs*o.period_ms/1000<1||o.prns.empty())throw std::invalid_argument("Invalid live engine rate/period/PRNs");
    if(!o.acquisition_threads||o.acquisition_threads>63||!o.tracking_threads||o.tracking_threads>63)
      throw std::invalid_argument("Acquisition/tracking threads must be integers in 1..63");
    if(std::abs(o.fs*.01-std::round(o.fs*.01))>.01)throw std::invalid_argument("Live RX rate exceeds .01 sample/code-period tolerance");
    auto prns=o.prns;std::sort(prns.begin(),prns.end());
    if(prns.front()<1||prns.back()>63||std::adjacent_find(prns.begin(),prns.end())!=prns.end())throw std::invalid_argument("Invalid live PRNs");
    o.acquisition.sample_rate_hz=o.fs;o.acquisition.if_hz=o.if_hz;
    o.tracking.sample_rate_hz=o.fs;o.tracking.if_hz=o.if_hz;o.tracking.carrier_rf_hz=o.rf_hz;
    bds_b1c_worker_pool_t* pool=nullptr;
    if(bds_b1c_worker_pool_create(std::min(o.tracking_threads,unsigned(o.prns.size())),&pool))
      throw std::runtime_error("Cannot create tracking worker pool");
    tracking_pool.reset(pool);
    for(int j=0;j<63;++j){c[j].owner=this;c[j].prn=j+1;if(bds_b1c_navigator_create(j+1,o.fs,&c[j].nav))throw std::runtime_error("navigator create failed");}
  }
  void event(Channel& ch,uint64_t sample,const char* name){emit("event",{{"prn",ch.prn},{"generation",counter(ch.generation)},{"absolute_sample",counter(sample)},{"time_s",sample/o.fs},{"event",name}});}
  void reset(uint64_t sample,const char* reason){
    for(auto& ch:c){if(ch.tracker){bds_b1c_tracker_destroy(ch.tracker);ch.tracker=nullptr;++ch.generation;}ch.rows.clear();ch.anchored=false;ch.reacquire=false;++ch.segment;bds_b1c_navigator_reset(ch.nav);}
    search.clear();pending.clear();initial=true;retry_at=sample;next_fix=last_fix=0;local=NAN;have_xyz=false;outer->active=0;outer->searching=true;
    emit("gap",{{"time_s",sample/o.fs},{"absolute_sample",counter(sample)},{"reason",reason}});
  }
  static void frame_callback(const bds_b1c_nav_frame_t* f,void* context){
    auto& ch=*static_cast<Channel*>(context);auto& self=*ch.owner;
    self.emit("frame",frame_json(*f,self.o.fs,ch.segment));
    if(f->ephemeris.valid){ch.anchor=*f;ch.anchored=true;++self.outer->frames;}
  }
  static void epoch_callback(const bds_b1c_tracking_epoch_t* r,void* context){
    auto& ch=*static_cast<Channel*>(context);auto e=*r;e.generation=ch.generation;ch.owner->accept(e);
  }
  static void collect_epoch(const bds_b1c_tracking_epoch_t* r,void* context) noexcept {
    auto& ch=*static_cast<Channel*>(context);
    if(ch.collect_failed)return;
    try{ch.pending_epochs.push_back(*r);}catch(...){ch.collect_failed=true;}
  }
  void accept(const bds_b1c_tracking_epoch_t& e){
    if(e.prn<1||e.prn>63||!e.sample_count||e.absolute_sample>UINT64_MAX-e.sample_count)throw std::runtime_error("invalid epoch");
    auto& ch=c[e.prn-1];ch.generation=e.generation;bool boundary=!ch.rows.empty()&&
      (ch.rows.back().generation!=e.generation||ch.rows.back().epoch+1!=e.epoch||ch.rows.back().absolute_sample+ch.rows.back().sample_count!=e.absolute_sample);
    if(boundary||!e.valid||(!ch.rows.empty()&&!ch.rows.back().valid)){++ch.segment;ch.anchored=false;}
    if(boundary){bds_b1c_navigator_reset(ch.nav);event(ch,e.absolute_sample,"sample_gap");}
    if(ch.rows.empty()||ch.rows.back().state!=e.state)event(ch,e.absolute_sample,e.state==BDS_B1C_LOCKED?"locked":e.state==BDS_B1C_LOST?"lost":"pull_in");
    ch.seen=true;ch.rows.push_back(e);if(ch.rows.size()>256)ch.rows.pop_front();
    emit("tracking",tracking_json(e,o.fs,ch.segment));
    if(bds_b1c_navigator_push(ch.nav,&e,frame_callback,&ch))throw std::runtime_error("navigator rejected epoch");
    if(e.state==BDS_B1C_LOST)ch.reacquire=true;
  }
  void feed(Channel& ch,const bds5g_cf32_t* data,size_t n,uint64_t start,bool collect=false){
    if(!ch.tracker||ch.reacquire)return;
    uint64_t next=bds_b1c_tracker_next_sample(ch.tracker);
    if(next>=start+n)return;
    if(next<start)throw std::runtime_error("tracker input continuity mismatch");
    size_t offset=size_t(next-start);
    if(bds_b1c_tracker_push(ch.tracker,data+offset,n-offset,next,collect?collect_epoch:epoch_callback,&ch))throw std::runtime_error("tracker rejected IQ block");
    if(ch.collect_failed)throw std::runtime_error("Cannot buffer tracking epochs");
  }
  void feed_all(const Block& block){
    if(o.tracking_threads==1){for(auto& ch:c)feed(ch,block.iq.data(),block.iq.size(),block.first);return;}
    for(auto& ch:c){ch.pending_epochs.clear();ch.collect_failed=false;}
    struct Batch{Impl* self;const Block* block;} batch{this,&block};
    auto task=[](size_t index,void* context){
      auto& batch=*static_cast<Batch*>(context);auto& self=*batch.self;const auto& b=*batch.block;
      self.feed(self.c[index],b.iq.data(),b.iq.size(),b.first,true);
    };
    if(bds_b1c_worker_pool_run(tracking_pool.get(),63,task,&batch))
      throw std::runtime_error("Parallel tracking failed (IQ continuity, input or allocation)");
    // Tracker state is private to its task. All shared navigation, generation,
    // event/log and receiver-clock state is committed in the original order.
    // The input block remains alive until every task has reached this barrier.
    for(auto& ch:c)for(auto epoch:ch.pending_epochs){epoch.generation=ch.generation;accept(epoch);}
  }
  void push(const Block& block){
    if(block.iq.empty())return;
    if(block.first>UINT64_MAX-block.iq.size())throw std::runtime_error("sample counter overflow");
    if(have_expected&&block.first<expected)throw std::runtime_error("RX input moved backwards");
    if(block.discontinuous||(have_expected&&block.first!=expected))reset(block.first,"rx_or_queue_discontinuity");
    have_expected=true;expected=block.first+block.iq.size();
    // Existing channels continue over each block while a fresh 20 ms search
    // window accumulates for lost channels. Newly acquired channels replay
    // that exact window; existing channels are never fed it twice.
    feed_all(block);
    if(pending.empty()&&block.first>=retry_at){
      if(initial)pending=o.prns;
      else for(int prn:o.prns){auto& ch=c[prn-1];if(ch.reacquire||(!ch.tracker&&block.first>=survey_at))pending.push_back(prn);}
      if(!pending.empty()){search_start=block.first;search.clear();outer->searching=true;}
    }
    if(!pending.empty()){
      search.insert(search.end(),block.iq.begin(),block.iq.end());
      if(search.size()>=size_t(std::llround(o.fs*.02))){
        std::vector<bds_b1c_acquisition_t> results(pending.size());
        if(bds_b1c_acquire_many_parallel(search.data(),search.size(),&o.acquisition,pending.data(),pending.size(),results.data(),o.acquisition_threads))throw std::runtime_error("live acquisition failed");
        bool acquired=false;
        for(const auto& result:results){
          auto& ch=c[result.prn-1];emit("acquisition",{{"prn",result.prn},{"acquired",int(result.acquired)},
            {"doppler_hz",result.doppler_hz},{"peak_metric",result.peak_metric},{"code_phase_samples",counter(result.code_phase_samples)},
            {"time_s",search_start/o.fs},{"absolute_sample",counter(search_start)}});
          if(!result.acquired)continue;
          acquired=true;if(ch.tracker){bds_b1c_tracker_destroy(ch.tracker);ch.tracker=nullptr;++ch.generation;}
          ++ch.segment;ch.rows.clear();ch.anchored=false;ch.reacquire=false;bds_b1c_navigator_reset(ch.nav);
          if(bds_b1c_tracker_create(&o.tracking,&result,search_start,&ch.tracker))throw std::runtime_error("live tracker create failed");
          event(ch,search_start,initial?"acquired":"reacquire_ok");feed(ch,search.data(),search.size(),search_start);
        }
        initial=initial&&!acquired;retry_at=expected+uint64_t(o.fs);
        if(expected>=survey_at)survey_at=expected+uint64_t(30*o.fs);
        search.clear();pending.clear();outer->searching=initial;
      }
    }
    unsigned active=0;for(auto& ch:c)active+=ch.tracker&&!ch.reacquire;outer->active=active;
    solve();outer->processed_sample=expected;
  }
  const bds_b1c_tracking_epoch_t* at(const Channel& ch,uint64_t sample){
    for(auto i=ch.rows.rbegin();i!=ch.rows.rend();++i)if(i->absolute_sample<=sample)return sample-i->absolute_sample<i->sample_count?&*i:nullptr;
    return nullptr;
  }
  void solve(){
    uint64_t target=UINT64_MAX,first=0,available=0;unsigned ready=0;int week_candidate=0;
    for(auto& ch:c)if(ch.anchored&&!ch.rows.empty()){
      target=std::min(target,ch.rows.back().absolute_sample+ch.rows.back().sample_count-1);
      if(ch.anchor.absolute_sample>=first){first=ch.anchor.absolute_sample;week_candidate=int(ch.anchor.ephemeris.WN);}
      available=std::max(available,ch.anchor.end_sample);++ready;
    }
    if(ready<4||target==UINT64_MAX)return;
    // Schedule on the nearest hardware sample; all time conversion retains fs.
    uint64_t step=uint64_t(std::llround(o.fs*o.period_ms/1000));
    if(!next_fix){base_week=week_candidate;uint64_t base=first+1;next_fix=base+(available>base?(available-base+step-1)/step:0)*step;}
    if(next_fix<available)next_fix+=(available-next_fix+step-1)/step*step;
    for(;next_fix<=target;next_fix+=step){
      const uint64_t sample=next_fix;
      bds_b1c_spp_observation_t obs[63];bds_b1c_time_t times[63];const bds_b1c_tracking_epoch_t* records[63];
      size_t used=0;double max_time=-INFINITY;Array excluded;
      auto exclude=[&](int prn,const char* reason){excluded.push_back(Object{{"prn",prn},{"reason",reason}});};
      for(auto& ch:c){
        auto r=at(ch,sample);
        if(!r||!r->valid){if(ch.seen)exclude(ch.prn,"tracking_unavailable");continue;}
        if(!ch.anchored||ch.anchor.end_sample>sample||ch.anchor.generation!=r->generation){exclude(ch.prn,"no_continuous_anchor");continue;}
        const auto& e=ch.anchor.ephemeris;
        if(e.HS||e.DIF||e.SIF||e.AIF){exclude(ch.prn,"health_or_integrity");continue;}
        bds_b1c_time_t tx;bds_b1c_satellite_t sat;
        if(bds_b1c_transmit_time(&ch.anchor,r,sample,o.fs,&tx)){exclude(ch.prn,"invalid_time_model");continue;}
        int code=bds_b1c_satellite_state(&e,tx,o.max_age,&sat);
        if(code){exclude(ch.prn,code==-2?"stale_ephemeris":code==-4?"unsupported_geo":"invalid_satellite_model");continue;}
        if(have_xyz){double az,el;if(bds_b1c_azimuth_elevation(xyz,sat.ecef_m,&az,&el)||el<o.elevation){exclude(ch.prn,"below_elevation_mask");continue;}}
        obs[used]={ch.prn,sat,0};times[used]=tx;records[used]=r;
        max_time=std::max(max_time,(double(tx.week)-base_week)*604800+tx.tow_s);++used;
      }
      if(!std::isfinite(local)&&used>=4)local=max_time+o.offset_ms/1000;
      else if(std::isfinite(local)&&last_fix)local+=double(sample-last_fix)/o.fs;
      double before=local;last_fix=sample;
      for(size_t j=0;j<used;++j)obs[j].pseudorange_m=(before-((double(times[j].week)-base_week)*604800+times[j].tow_s))*BDS_B1C_SPEED_OF_LIGHT;
      bds_b1c_spp_solution_t s{};int status=used<4?-2:bds_b1c_spp_solve(obs,used,o.tropo,&s);
      if(!status){local-=s.clock_correction_m/BDS_B1C_SPEED_OF_LIGHT;std::memcpy(xyz,s.ecef_m,sizeof(xyz));have_xyz=true;++outer->fixes;}
      Object result={{"index",counter(index++)},{"absolute_sample",counter(sample)},{"time_s",sample/o.fs},
        {"available_s",sample/o.fs},{"satellites",unsigned(used)},{"valid",int(!status)},
        {"status",!status?"ok":status==-2?"insufficient_satellites":status==-3?"singular_geometry":"no_convergence"}};
      if(!status){
        result["x_m"]=s.ecef_m[0];result["y_m"]=s.ecef_m[1];result["z_m"]=s.ecef_m[2];
        result["latitude_deg"]=s.latitude_deg;result["longitude_deg"]=s.longitude_deg;result["height_m"]=s.height_m;
        result["clock_correction_m"]=s.clock_correction_m;result["residual_rms_m"]=s.residual_rms_m;
        result["gdop"]=s.gdop;result["pdop"]=s.pdop;result["hdop"]=s.hdop;result["vdop"]=s.vdop;result["tdop"]=s.tdop;
      }
      Array observations;
      for(size_t j=0;j<used;++j){double raw=obs[j].pseudorange_m,clock=obs[j].satellite.clock_correction_s;
        observations.push_back(Object{{"prn",obs[j].prn},{"generation",counter(records[j]->generation)},{"epoch",counter(records[j]->epoch)},
          {"absolute_sample",counter(sample)},{"used",int(!status)},{"raw_pseudorange_m",raw},{"satellite_clock_s",clock},
          {"clock_corrected_pseudorange_m",raw+clock*BDS_B1C_SPEED_OF_LIGHT},
          {"receiver_corrected_pseudorange_m",status?Json():Json(raw+clock*BDS_B1C_SPEED_OF_LIGHT-s.clock_correction_m)},
          {"elevation_deg",status?Json():Json(s.elevation_deg[j])},{"azimuth_deg",status?Json():Json(s.azimuth_deg[j])},
          {"residual_m",status?Json():Json(s.residual_m[j])}});
      }
      emit("position",{{"solution",result},{"observations",observations},{"exclusions",excluded}});
    }
  }
};
Engine::Engine(Options o,Emit emit):p_(new Impl(this,std::move(o),std::move(emit))){}
Engine::~Engine()=default;
void Engine::push(const Block& b){p_->push(b);}
void Engine::epoch(const bds_b1c_tracking_epoch_t& e){p_->accept(e);}
void Engine::finish_epochs(){p_->solve();}
void Engine::gap(uint64_t sample,const char* reason){p_->reset(sample,reason);p_->have_expected=false;}
}

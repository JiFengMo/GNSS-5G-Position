// RX-only B210 application. DSP lives in the transport-independent Engine.
#include "live_engine.hpp"
#include "live_log.hpp"
#include "uhd_b210.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

using namespace b1live;
static volatile std::sig_atomic_t interrupted=0;
static void stop_signal(int){interrupted=1;}
static std::map<std::string,std::string> arguments(int argc,char** argv){
  std::map<std::string,std::string> result;
  for(int j=1;j<argc;++j){std::string key=argv[j];if(key=="--help"){std::cout<<"bds-b1c-live --device-args type=b200,serial=... --sample-rate HZ --clock-source external --time-source external --output-dir NEW_DIR --run-id ID [RF/loop options]\nRX-only; Ctrl+C stops streaming. No IQ-file fallback or transmit streamer.\n";std::exit(0);}if(key.rfind("--",0)!=0||j+1>=argc||result.count(key))throw std::runtime_error("Invalid/duplicate option: "+key);result[key]=argv[++j];}
  return result;
}
int main(int argc,char** argv){
  std::signal(SIGINT,stop_signal);std::signal(SIGTERM,stop_signal);
  try {
    auto args=arguments(argc,argv);
    auto text=[&](const char* k,const char* fallback){auto i=args.find(k);std::string v=i==args.end()?fallback:i->second;if(i!=args.end())args.erase(i);return v;};
    auto number=[&](const char* k,double fallback){std::string v=text(k,std::to_string(fallback).c_str());size_t n;double d=std::stod(v,&n);if(n!=v.size()||!std::isfinite(d))throw std::runtime_error(std::string("Invalid number ")+k);return d;};
    std::string device=text("--device-args","type=b200"),serial=text("--serial",""),clock=text("--clock-source","external"),time=text("--time-source","external"),antenna=text("--antenna","RX2");
    if(clock!="external"&&clock!="internal")throw std::runtime_error("Invalid clock source");
    if(time!="external"&&time!="internal")throw std::runtime_error("Invalid time source");
    // Serial has one unambiguous source, including when it is empty.
    for(size_t start=0;start<device.size();){size_t end=device.find(',',start);auto item=device.substr(start,end-start);if(item.rfind("serial=",0)==0)throw std::runtime_error("Use --serial instead of serial in --device-args");if(end==std::string::npos)break;start=end+1;}
    if(!serial.empty()){
      if(serial.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-")!=std::string::npos)throw std::runtime_error("Invalid B210 serial");
      device+=",serial="+serial;
    }
    std::string directory=text("--output-dir",""),id=text("--run-id",""),list=text("--prns","1-63");
    if(directory.empty()||id.empty())throw std::runtime_error("--output-dir and --run-id required");
    Options options;options.fs=number("--sample-rate",4092000);options.rf_hz=number("--frequency",1575420000);options.if_hz=number("--if",0);
    double bandwidth=number("--bandwidth",4000000),gain=number("--gain",42),master=number("--master-clock-rate",32736000),queue_s=number("--queue-seconds",16);
    options.acquisition.doppler_min_hz=number("--doppler-min",-8000);options.acquisition.doppler_max_hz=number("--doppler-max",8000);
    options.acquisition.doppler_step_hz=number("--doppler-step",250);options.acquisition.fine_step_hz=number("--fine-step",25);
    options.acquisition.threshold=number("--threshold",10);
    options.tracking.dll_bandwidth_hz=number("--dll-bandwidth",1);options.tracking.pll_bandwidth_hz=number("--pll-bandwidth",18);
    options.tracking.early_late_offset_chips=number("--early-late",.06);
    options.period_ms=number("--period-ms",200);options.elevation=number("--elevation-mask",5);
    if(!args.empty())throw std::runtime_error("Unknown option: "+args.begin()->first);
    if(options.fs<2046000||options.fs>1e8||queue_s<.1||queue_s>60||options.fs*queue_s*8>2.0*1024*1024*1024)throw std::runtime_error("Invalid sample rate/queue memory (maximum 2 GiB)");
    size_t cursor=0;while(cursor<list.size()){
      size_t comma=list.find(',',cursor);std::string item=list.substr(cursor,comma==std::string::npos?comma:comma-cursor);size_t dash=item.find('-');
      size_t n=0;int first=std::stoi(item,&n);int last=first;
      if(dash!=std::string::npos){if(n!=dash)throw std::runtime_error("Invalid PRN list");std::string tail=item.substr(dash+1);last=std::stoi(tail,&n);if(n!=tail.size())throw std::runtime_error("Invalid PRN range");}
      else if(n!=item.size())throw std::runtime_error("Invalid PRN list");
      if(first<1||last>63||last<first)throw std::runtime_error("PRNs must be 1..63");
      for(int p=first;p<=last;++p){if(std::find(options.prns.begin(),options.prns.end(),p)!=options.prns.end())throw std::runtime_error("Duplicate PRN");options.prns.push_back(p);}
      if(comma==std::string::npos)break;
      cursor=comma+1;
    }
    if(options.prns.empty())throw std::runtime_error("No PRNs selected");
    b1_radio_options_t radio_options={device.c_str(),clock.c_str(),time.c_str(),antenna.c_str(),options.fs,options.rf_hz-options.if_hz,bandwidth,gain,master};
    b1_radio_t* raw=nullptr;b1_radio_info_t info;char error[1024];
    if(b1_radio_open(&radio_options,&info,&raw,&interrupted,error,sizeof(error)))throw std::runtime_error(error);
    std::unique_ptr<b1_radio_t,decltype(&b1_radio_close)> radio(raw,b1_radio_close);
    options.fs=info.sample_rate;
    if(!std::isfinite(options.fs)||options.fs<2046000||options.fs>1e8||options.fs*queue_s*8>2.0*1024*1024*1024)throw std::runtime_error("Actual RX sample rate/queue exceeds supported limits");
    if(std::abs(options.fs*.01-std::round(options.fs*.01))>1e-5)throw std::runtime_error("Actual RX rate has a noninteger 10 ms code period");
    options.if_hz=options.rf_hz-info.frequency;
    Json reference=info.reference_locked<0?Json():Json(bool(info.reference_locked));
    Array prns;for(int p:options.prns)prns.push_back(p);
    Object run={{"schema",1},{"kind","b1c_live"},{"run_id",id},{"name",id},{"sample_rate_hz",options.fs},{"prns",prns},{"offline_retrospective",false},
      {"tracking",Object{{"if_hz",options.if_hz},{"pll_bandwidth_hz",options.tracking.pll_bandwidth_hz},{"dll_bandwidth_hz",options.tracking.dll_bandwidth_hz},{"early_late_offset_chips",options.tracking.early_late_offset_chips},{"quality_epochs",options.tracking.quality_epochs}}},
      {"navigation",Object{{"ldpc_error_correction",false}}},{"positioning",Object{{"period_ms",options.period_ms},{"elevation_mask_deg",options.elevation},{"troposphere",options.tropo}}},
      {"device",Object{{"args",info.address},{"requested_args",device},{"rf_hz",options.rf_hz},{"tuned_hz",info.frequency},{"gain_db",info.gain},{"bandwidth_hz",info.bandwidth},{"antenna",info.antenna},{"clock_source",clock},{"time_source",time},{"reference_locked",reference},{"pps_detected",bool(info.pps_detected)}}}};
    Log log(directory,id,run);Queue queue(size_t(options.fs*queue_s));
    Engine engine(options,[&](const char* kind,const Object& data){log.emit(kind,data);});
    std::atomic<bool> stop{false},failed{false};std::atomic<uint64_t> received{0},latest{0},overflows{0},timeouts{0},gaps{0};
    std::mutex error_mutex;std::string failure;
    auto fail=[&](const std::exception& e){std::lock_guard<std::mutex> lock(error_mutex);failure=e.what();failed=true;stop=true;queue.close();};
    std::thread worker([&]{try{Block b;while(!stop&&queue.pop(b))engine.push(b);}catch(const std::exception& e){fail(e);}});
    std::thread monitor;
    try {monitor=std::thread([&]{try{while(!stop){
      uint64_t end=latest.load(),processed=engine.processed_sample.load();
      int ref,pps;if(b1_radio_clock(radio.get(),&ref,&pps))throw std::runtime_error(b1_radio_error(radio.get()));
      if(clock=="external"&&ref==0)throw std::runtime_error("External 10 MHz reference lost lock");
      if(time=="external"&&!pps)throw std::runtime_error("External PPS stopped");
      log.emit("runtime",{{"running",true},{"received_samples",counter(received.load())},{"rx_sample",counter(end)},{"processed_sample",counter(processed)},
        {"queue_samples",uint64_t(queue.samples())},{"queue_dropped_samples",counter(queue.dropped_samples.load())},{"queue_overruns",counter(queue.overruns.load())},
        {"rx_overflows",counter(overflows.load())},{"rx_timeouts",counter(timeouts.load())},{"rx_gaps",counter(gaps.load())},
        {"lag_s",end>=processed?double(end-processed)/options.fs:0},{"stage",engine.searching?"searching":"tracking"},
        {"active_channels",engine.active.load()},{"valid_fixes",counter(engine.fixes.load())},{"valid_frames",counter(engine.frames.load())},
        {"reference_locked",ref<0?Json():Json(bool(ref))},{"pps_detected",bool(pps)}});
      for(int j=0;j<5&&!stop;++j)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }}catch(const std::exception& e){fail(e);}});}catch(...){stop=true;queue.close();worker.join();throw;}
    try {
      if(b1_radio_start(radio.get()))throw std::runtime_error(b1_radio_error(radio.get()));
      std::cout<<"BDS B210 RX started: "<<info.address<<" fs="<<options.fs<<" RF="<<info.frequency<<"\n"<<std::flush;
      uint64_t origin=0,expected_tick=0;bool have_origin=false,discontinuity=false;int consecutive_timeouts=0;
      std::vector<bds5g_cf32_t> samples(65536);
      while(!stop&&!interrupted){
        b1_radio_packet_t packet;if(b1_radio_receive(radio.get(),samples.data(),samples.size(),&packet))throw std::runtime_error(b1_radio_error(radio.get()));
        size_t count=packet.count;
        if(packet.error==8){++overflows;discontinuity=true;continue;}
        if(packet.error==1){++timeouts;if(++consecutive_timeouts>20)throw std::runtime_error("BDS RX timed out for 6 seconds");continue;}
        if(packet.error!=0)throw std::runtime_error("BDS RX metadata error "+std::to_string(packet.error));
        if(!count)continue;
        consecutive_timeouts=0;
        uint64_t first=packet.tick;
        if(!have_origin){origin=first;expected_tick=first;have_origin=true;log.emit("hardware_origin",{{"hardware_sample",counter(origin)},{"sample_rate_hz",options.fs}});}
        if(first<origin||first<expected_tick)throw std::runtime_error("RX hardware time moved backwards");
        bool broken=discontinuity||first!=expected_tick;
        if(broken){++gaps;discontinuity=false;}
        if(first>UINT64_MAX-count)throw std::runtime_error("RX sample counter overflow");
        expected_tick=first+count;Block block;block.first=first-origin;block.discontinuous=broken;block.iq.resize(count);
        std::copy_n(samples.begin(),count,block.iq.begin());
        received+=count;latest=block.first+count;queue.push(std::move(block));
      }
    }catch(const std::exception& e){fail(e);}
    b1_radio_stop(radio.get());
    stop=true;queue.close();worker.join();monitor.join();
    log.emit("runtime",{{"running",false},{"stage",failed?"error":"stopped"},{"error",failure},{"received_samples",counter(received.load())},{"processed_sample",counter(engine.processed_sample.load())}});
    if(failed){std::cerr<<failure<<'\n';return 3;}return 0;
  }catch(const std::exception& e){std::cerr<<"BDS live receiver: "<<e.what()<<'\n';return 2;}
}

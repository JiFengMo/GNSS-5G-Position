#include "bds5g/b1c_parallel.h"
#include "live_engine.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

extern "C" int b1c_serial_reference_acquire_many(const bds5g_cf32_t*,size_t,
  const bds_b1c_acquisition_options_t*,const int*,size_t,bds_b1c_acquisition_t*);
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool bits(double a,double b){return std::memcmp(&a,&b,sizeof(a))==0;}
static void equal(const bds_b1c_acquisition_t& a,const bds_b1c_acquisition_t& b){
  check(a.prn==b.prn&&a.acquired==b.acquired&&a.code_phase_samples==b.code_phase_samples,"Acquisition decisions/phase changed");
#define SAME(field) check(bits(a.field,b.field),"Acquisition " #field " bits changed");
  SAME(doppler_hz) SAME(peak_metric) SAME(cn0_db_hz) SAME(carrier_hz) SAME(coarse_doppler_hz) SAME(fine_metric)
#undef SAME
}
static void pool_test(){
  bds_b1c_worker_pool_t* raw=nullptr;
  check(bds_b1c_worker_pool_create(0,&raw)==-1&&!raw,"Invalid pool accepted");
  for(unsigned threads:{1U,4U}){
    check(!bds_b1c_worker_pool_create(threads,&raw),"Pool creation failed");
    std::unique_ptr<bds_b1c_worker_pool_t,decltype(&bds_b1c_worker_pool_destroy)> pool(raw,bds_b1c_worker_pool_destroy);
    std::vector<unsigned> counts(257);
    auto task=[](size_t j,void* p){++(*static_cast<std::vector<unsigned>*>(p))[j];if(j==7)throw std::runtime_error("test exception");};
    check(bds_b1c_worker_pool_run(pool.get(),counts.size(),task,&counts)==-3,"Task failure must propagate after barrier");
    for(auto n:counts)check(n==1,"Batch did not finish all tasks exactly once");
    auto success=[](size_t j,void* p){++(*static_cast<std::vector<unsigned>*>(p))[j];};
    check(!bds_b1c_worker_pool_run(pool.get(),counts.size(),success,&counts),"Pool not reusable after failure");
    for(auto n:counts)check(n==2,"Batch result lost");
    check(!bds_b1c_worker_pool_run(pool.get(),0,success,&counts),"Empty batch failed");
  }
}
static void acquisition_test(){
  const int prns[]={37,23,46};
  for(double fs:{2046000.,4092000.0037})for(double coherent:{1.,5.,10.}){
    bds_b1c_acquisition_options_t o;bds_b1c_acquisition_defaults(&o);
    o.sample_rate_hz=fs;o.coherent_integration_ms=coherent;o.doppler_min_hz=-50;o.doppler_max_hz=50;
    // 1/5 ms exercise the direct-mix fallback; 10 ms uses shared FFT shifts.
    const size_t period=size_t(std::llround(fs*.01));
    std::vector<int8_t> code(period);check(!bds_b1c_sample_boc11(23,BDS_B1C_DATA,fs,code.data(),period),"Code failed");
    std::vector<bds5g_cf32_t> iq(2*period);
    for(size_t n=0;n<iq.size();++n){double phase=2*3.14159265358979323846*25*n/fs;double a=code[(n+period-123)%period];iq[n]={float(a*cos(phase)+.01*sin(n*1.23)),float(a*sin(phase)+.01*cos(n*2.34))};}
    bds_b1c_acquisition_t reference[3],actual[3];
    check(!b1c_serial_reference_acquire_many(iq.data(),iq.size(),&o,prns,3,reference),"Frozen serial reference failed");
    for(unsigned threads:{1U,2U,8U}){
      check(!bds_b1c_acquire_many_parallel(iq.data(),iq.size(),&o,prns,3,actual,threads),"Parallel acquisition failed");
      for(int j=0;j<3;++j)equal(reference[j],actual[j]);
    }
    // Two independent calls may overlap. Only FFTW planning is shared/locked.
    bds_b1c_acquisition_t other[3];int error=0;
    std::thread peer([&]{error=bds_b1c_acquire_many_parallel(iq.data(),iq.size(),&o,prns,3,other,2);});
    int status=bds_b1c_acquire_many(iq.data(),iq.size(),&o,prns,3,actual);peer.join();
    check(!status&&!error,"Independent acquisition calls failed");
    for(int j=0;j<3;++j){equal(reference[j],actual[j]);equal(reference[j],other[j]);}
    check(bds_b1c_acquire_many_parallel(iq.data(),iq.size(),&o,prns,3,actual,0)==-1,"Invalid threads accepted");
    iq[0].i=NAN;
    check(bds_b1c_acquire_many_parallel(iq.data(),iq.size(),&o,prns,3,actual,2)==-1,"Nonfinite input accepted");
  }
}
static void live_test(){
  using namespace b1live;
  Options o;o.fs=2046000;o.prns={23,37,46};
  o.acquisition.doppler_min_hz=-100;o.acquisition.doppler_max_hz=100;
  const size_t period=size_t(std::llround(o.fs*.01)),count=14*period;
  std::vector<bds5g_cf32_t> iq(count);
  for(size_t p=0;p<o.prns.size();++p){
    std::vector<int8_t> data(period),pilot(period);int8_t secondary[1800];
    check(!bds_b1c_sample_boc11(o.prns[p],BDS_B1C_DATA,o.fs,data.data(),period),"Data failed");
    check(!bds_b1c_sample_boc11(o.prns[p],BDS_B1C_PILOT,o.fs,pilot.data(),period),"Pilot failed");
    check(!bds_b1c_generate_secondary_code(o.prns[p],secondary,1800),"Secondary failed");
    for(size_t n=0;n<count;++n){size_t offset=n+period-123*(p+1),k=offset%period,epoch=offset/period;
      double a=.524404424*data[k]*(epoch%2?-1:1),b=.851469318*pilot[k]*secondary[epoch%1800];
      double phase=2*3.14159265358979323846*(double(p)-1)*50*n/o.fs;
      iq[n].i+=float(a*cos(phase)-b*sin(phase));iq[n].q+=float(a*sin(phase)+b*cos(phase));
    }
  }
  for(size_t chunk:{size_t(8192),size_t(65536)}){
    std::vector<std::string> reference;
    for(unsigned threads:{1U,2U,4U}){
      o.acquisition_threads=threads;o.tracking_threads=threads;
      std::vector<std::string> records;unsigned tracking=0,captures=0;
      Engine engine(o,[&](const char* kind,const Object& data){
        records.push_back(Json(Object{{"kind",kind},{"data",data}}).dump());
        tracking+=std::string(kind)=="tracking";
        captures+=std::string(kind)=="acquisition"&&data.at("acquired").dump()=="1";
      });
      for(size_t n=0;n<count;n+=chunk){Block block;block.first=n;size_t end=std::min(count,n+chunk);block.iq.assign(iq.begin()+n,iq.begin()+end);engine.push(block);}
      check(tracking>10&&captures==3,"Parallel live test needs multiple acquired channels");
      Block gap;gap.first=count+100;gap.discontinuous=true;gap.iq.assign(iq.begin(),iq.begin()+2*period);engine.push(gap);
      if(threads==1)reference=records;else check(records==reference,"Live event content/order differs from serial");
    }
  }
}
int main(){try{pool_test();acquisition_test();live_test();std::cout<<"PASS: frozen serial equivalence, independent acquisition calls, pool barriers/exceptions and ordered live output\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

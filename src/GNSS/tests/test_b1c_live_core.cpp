#include "live_engine.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace b1live;
static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static std::vector<std::string> split(const std::string& s){std::vector<std::string> out;size_t p=0;while(true){size_t n=s.find(',',p);out.push_back(s.substr(p,n==std::string::npos?n:n-p));if(n==std::string::npos)break;p=n+1;}return out;}
static void iq_stream_test(double fs){
  Options o;o.fs=fs;o.prns={23};o.if_hz=20000;o.acquisition.doppler_min_hz=-250;o.acquisition.doppler_max_hz=250;
  const size_t period=size_t(std::llround(o.fs*.01)),total=12*period;
  std::vector<int8_t> data(period),pilot(period);int8_t secondary[1800];
  check(!bds_b1c_sample_boc11(23,BDS_B1C_DATA,o.fs,data.data(),period),"Data replica");
  check(!bds_b1c_sample_boc11(23,BDS_B1C_PILOT,o.fs,pilot.data(),period),"Pilot replica");
  check(!bds_b1c_generate_secondary_code(23,secondary,1800),"Secondary replica");
  std::vector<bds5g_cf32_t> iq(total);
  for(size_t n=0;n<total;++n){
    size_t index=(n+period-1234)%period,epoch=(n+period-1234)/period;
    double a=.524404424*data[index]*(epoch%2?-1:1),b=.851469318*pilot[index]*secondary[epoch%1800];
    double phase=2*3.14159265358979323846*20125*n/o.fs;
    iq[n]={float(a*cos(phase)-b*sin(phase)+.01*sin(n*1.345)),float(a*sin(phase)+b*cos(phase)+.01*cos(n*2.345))};
  }
  std::vector<std::string> reference;
  for(size_t chunk:{size_t(777),size_t(65536)}){
    std::vector<std::string> epochs;unsigned captures=0;Engine e(o,[&](const char* kind,const Object& d){
      if(std::string(kind)=="tracking"){
        epochs.push_back(Json(d).dump());
        uint64_t first=std::stoull(d.at("absolute_sample").dump().substr(1));
        check(std::abs(std::stod(d.at("time_s").dump())-first/fs)<1e-14,"Tracking time must retain actual hardware rate");
      }
      if(std::string(kind)=="acquisition"&&d.at("acquired").dump()=="1")++captures;
    });
    for(size_t n=0;n<total;n+=chunk){Block b;b.first=n;size_t count=std::min(chunk,total-n);b.iq.assign(iq.begin()+n,iq.begin()+n+count);e.push(b);}
    check(captures==1&&e.active==1&&epochs.size()>=10,"Live IQ acquisition/tracking failed");
    if(reference.empty())reference=epochs;else check(reference==epochs,"Live block boundaries changed epochs or replayed samples twice");
  }
}
int main(int argc,char** argv){try{
  if(argc==3){
    Options o;o.fs=std::stod(argv[2]);o.prns={23,25,27,28,37,43,46};
    Engine engine(o,[](const char* kind,const Object& data){std::cout<<Json(Object{{"kind",kind},{"data",data}}).dump()<<'\n';});
    std::ifstream file(argv[1]);check(bool(file),"Cannot open epoch vectors");std::string line;std::getline(file,line);auto names=split(line);std::map<std::string,size_t> columns;
    for(size_t j=0;j<names.size();++j)columns[names[j]]=j;
    while(std::getline(file,line)){
      auto values=split(line);auto value=[&](const char* k)->const std::string&{return values.at(columns.at(k));};
      bds_b1c_tracking_epoch_t e{};e.prn=std::stoi(value("prn"));e.epoch=std::stoull(value("epoch"));e.generation=std::stoull(value("generation"));
      e.absolute_sample=std::stoull(value("absolute_sample"));e.sample_count=std::stoull(value("sample_count"));e.integration_s=std::stod(value("integration_s"));
      e.code_rate_hz=std::stod(value("code_rate_hz"));e.rem_code_phase=std::stod(value("rem_code_phase"));e.valid=std::stoi(value("valid"));
      e.state=value("state")=="locked"?BDS_B1C_LOCKED:value("state")=="lost"?BDS_B1C_LOST:BDS_B1C_PULL_IN;
      e.data_prompt.i=std::stod(value("data_i_p"));e.pilot_prompt.q=std::stod(value("pilot_q_p"));
      engine.epoch(e);engine.finish_epochs();
    }
    return 0;
  }
  Queue queue(8);Block a;a.first=0;a.iq.resize(4);queue.push(a);a.first=4;queue.push(a);a.first=8;a.discontinuous=true;queue.push(a);
  check(queue.dropped_samples==8&&queue.overruns==1,"Queue overflow must discard old backlog");
  Block b;check(queue.pop(b)&&b.first==8&&b.discontinuous,"Queue lost newest block/metadata");queue.close();check(!queue.pop(b),"Closed queue must wake the consumer");
  check(Json(NAN).dump()=="null","NaN must serialize to null");
  check(Json(Object{{"counter",counter(UINT64_C(9007199254741115))},{"quote","a\"b\nc"}}).dump().find("9007199254741115")!=std::string::npos,"uint64 counter precision");
  Options bad;bad.prns={23};bad.period_ms=0;bool rejected=false;
  try{Engine e(bad,[](const char*,const Object&){});}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"Zero solve interval must be rejected");
  Options o;o.prns={23};o.acquisition.doppler_min_hz=o.acquisition.doppler_max_hz=0;
  unsigned gaps=0,positions=0;Engine engine(o,[&](const char* kind,const Object&){gaps+=std::string(kind)=="gap";positions+=std::string(kind)=="position";});
  a.first=0;a.discontinuous=false;a.iq.resize(1);engine.push(a);a.first=5;engine.push(a);
  check(gaps==1&&engine.processed_sample==6,"Sample gap must reset continuity");
  rejected=false;a.first=3;try{engine.push(a);}catch(const std::runtime_error&){rejected=true;}check(rejected,"Backward input must be rejected");
  Engine zero(o,[&](const char* kind,const Object&){positions+=std::string(kind)=="position";});
  a.first=0;a.iq.assign(size_t(o.fs*.02),{});zero.push(a);check(!zero.active&&positions==0,"Zero IQ cannot produce lock/coordinates");
  for(double fs:{4092000.,4092000.0037,4091999.9963,4092000.75})iq_stream_test(fs);
  bad.fs=4092020;bad.period_ms=200;rejected=false;
  try{Engine e(bad,[](const char*,const Object&){});}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"Substantially noninteger acquisition periods must still be rejected");
  std::cout<<"PASS: bounded queue, counter/JSON encoding, gaps, invalid settings and zero IQ\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

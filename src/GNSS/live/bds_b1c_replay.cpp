// File-only validation of the production Queue + Engine; never opens UHD.
#include "live_engine.hpp"
#include "../app/b1c_file_io.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace b1live;
using Clock=std::chrono::steady_clock;
static double elapsed(Clock::time_point start){return std::chrono::duration<double>(Clock::now()-start).count();}
int main(int argc,char** argv){try{
  std::map<std::string,std::string> args;
  for(int j=1;j<argc;++j){
    std::string key=argv[j];
    if(key=="--help"){
      std::cout<<"bds-b1c-replay --input IQ --output-dir NEW_DIR [--input-format ci8|cf32] [--sample-rate HZ] [--if HZ] [--seconds S] [--prns LIST] [--acquisition-threads N] [--tracking-threads N] [--paced 0|1] [--queue-seconds S] [--chunk-samples N]\n"
        "File-only production Queue + Engine. Defaults: 4.092 Msps, IF 0, all PRNs, 1/1 threads, paced=0, queue=16 s, chunk=65536, Doppler +/-8000/250/25 Hz.\n"
        "Outputs: events.jsonl (deterministic kind/data), timeline.jsonl, summary.json. Continuous IQ required; finite replay is not a hardware real-time acceptance.\n";return 0;
    }
    if(key.rfind("--",0)!=0||j+1>=argc||args.count(key))throw std::invalid_argument("Invalid/duplicate argument: "+key);
    args[key]=argv[++j];
  }
  auto text=[&](const char* key,const char* fallback){auto i=args.find(key);if(i==args.end())return std::string(fallback);auto value=i->second;args.erase(i);return value;};
  auto number=[&](const char* key,double fallback){auto value=text(key,std::to_string(fallback).c_str());size_t used;double n=std::stod(value,&used);if(used!=value.size()||!std::isfinite(n))throw std::invalid_argument(key);return n;};
  auto integer=[&](const char* key,unsigned fallback,unsigned maximum){double n=number(key,fallback);if(n<1||n>maximum||n!=std::floor(n))throw std::invalid_argument(key);return unsigned(n);};
  const std::string input=text("--input",""),directory=text("--output-dir",""),format=text("--input-format","cf32"),list=text("--prns","1-63");
  if(input.empty()||directory.empty()||(format!="ci8"&&format!="cf32"))throw std::invalid_argument("Input/output/format required");
  Options options;options.fs=number("--sample-rate",4092000);options.if_hz=number("--if",0);
  options.acquisition_threads=integer("--acquisition-threads",1,63);options.tracking_threads=integer("--tracking-threads",1,63);
  const unsigned chunk=integer("--chunk-samples",65536,1000000);
  const double seconds=number("--seconds",0),queue_s=number("--queue-seconds",16),pace=number("--paced",0);
  options.acquisition.doppler_min_hz=number("--doppler-min",-8000);options.acquisition.doppler_max_hz=number("--doppler-max",8000);
  options.acquisition.doppler_step_hz=number("--doppler-step",250);options.acquisition.fine_step_hz=number("--fine-step",25);
  options.acquisition.threshold=number("--threshold",10);
  if(!args.empty())throw std::invalid_argument("Unknown argument: "+args.begin()->first);
  if(options.fs<2046000||options.fs>1e8||queue_s<.1||queue_s>60||options.fs*queue_s*8>2.*1024*1024*1024||
     seconds<0||seconds>3600||(pace!=0&&pace!=1))throw std::invalid_argument("Invalid rate, duration, pacing or queue");
  size_t cursor=0;
  while(cursor<list.size()){
    size_t comma=list.find(',',cursor);std::string item=list.substr(cursor,comma==std::string::npos?comma:comma-cursor);
    size_t dash=item.find('-'),used=0;int first=std::stoi(item,&used),last=first;
    if(dash!=std::string::npos){if(used!=dash)throw std::invalid_argument("PRN list");auto tail=item.substr(dash+1);last=std::stoi(tail,&used);if(used!=tail.size())throw std::invalid_argument("PRN range");}
    else if(used!=item.size())throw std::invalid_argument("PRN list");
    if(first<1||last>63||last<first)throw std::invalid_argument("PRNs must be 1..63");
    for(int p=first;p<=last;++p)options.prns.push_back(p);
    if(comma==std::string::npos)break;
    cursor=comma+1;if(cursor==list.size())throw std::invalid_argument("Trailing PRN comma");
  }
  b1c_file_reader_t raw_reader{};
  if(b1c_file_open(&raw_reader,input.c_str(),format=="ci8"))throw std::runtime_error("Cannot open IQ");
  std::unique_ptr<b1c_file_reader_t,decltype(&b1c_file_close)> reader(&raw_reader,b1c_file_close);
  const uint64_t available=std::filesystem::file_size(input)/(format=="ci8"?2U:8U);
  if(std::filesystem::file_size(input)%(format=="ci8"?2U:8U))throw std::runtime_error("Partial IQ sample");
  const uint64_t limit=seconds>0?uint64_t(std::llround(options.fs*seconds)):available;
  if(!limit||limit>available)throw std::runtime_error("Empty/short IQ input");
  if(!std::filesystem::create_directory(directory))throw std::runtime_error("Output must be a new directory with an existing parent");
  std::ofstream events(std::filesystem::path(directory)/"events.jsonl"),timeline(std::filesystem::path(directory)/"timeline.jsonl");
  if(!events||!timeline)throw std::runtime_error("Cannot create replay outputs");
  Queue queue(size_t(options.fs*queue_s));
  Engine engine(options,[&](const char* kind,const Object& data){events<<Json(Object{{"kind",kind},{"data",data}}).dump()<<'\n';if(!events)throw std::runtime_error("Cannot write replay events");});
  std::atomic<bool> failed{false};std::atomic<uint64_t> received{0};
  std::exception_ptr worker_error;double max_latency=0,engine_s=0;
  const auto start=Clock::now();
  std::thread worker([&]{try{
    Block block;while(queue.pop(block)){
      const auto before=Clock::now();engine.push(block);const double compute=elapsed(before);engine_s+=compute;
      const double wall=elapsed(start),latency=wall-double(block.first+block.iq.size())/options.fs;
      max_latency=std::max(max_latency,latency);
      timeline<<Json(Object{{"wall_s",wall},{"processing_s",compute},{"processed_sample",counter(engine.processed_sample.load())},
        {"received_samples",counter(received.load())},{"queue_samples",counter(queue.samples())},{"completion_latency_s",pace==1?Json(latency):Json()},
        {"dropped_samples",counter(queue.dropped_samples.load())},{"active_channels",engine.active.load()},{"searching",engine.searching.load()}}).dump()<<'\n';
      if(!timeline)throw std::runtime_error("Cannot write replay timeline");
    }
  }catch(...){worker_error=std::current_exception();failed=true;queue.close();}});
  std::exception_ptr producer_error;
  try{
    uint64_t position=0;
    while(position<limit&&!failed){
      Block block;block.first=position;block.iq.resize(size_t(std::min<uint64_t>(chunk,limit-position)));
      size_t got=0;if(b1c_file_read(reader.get(),block.iq.data(),block.iq.size(),&got)||got!=block.iq.size())throw std::runtime_error("Short/invalid IQ read");
      position+=got;
      if(pace==1)std::this_thread::sleep_until(start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(double(position)/options.fs)));
      received=position;queue.push(std::move(block));
    }
  }catch(...){producer_error=std::current_exception();}
  queue.close();worker.join();
  if(worker_error)std::rethrow_exception(worker_error);
  if(producer_error)std::rethrow_exception(producer_error);
  events.close();timeline.close();if(!events||!timeline)throw std::runtime_error("Cannot close replay outputs");
  const double wall=elapsed(start);
  Object summary={{"sample_rate_hz",options.fs},{"input_format",format},{"input",input},{"if_hz",options.if_hz},
    {"signal_s",double(limit)/options.fs},{"wall_s",wall},{"engine_s",engine_s},{"paced",pace==1},
    {"signal_per_wall",queue.dropped_samples.load()?Json():Json(double(limit)/options.fs/wall)},
    {"acquisition_threads",options.acquisition_threads},{"tracking_threads",options.tracking_threads},{"chunk_samples",chunk},
    {"queue_seconds",queue_s},{"queue_peak_samples",counter(queue.peak_samples.load())},{"queue_dropped_samples",counter(queue.dropped_samples.load())},
    {"queue_overruns",counter(queue.overruns.load())},{"max_completion_latency_s",pace==1?Json(max_latency):Json()},
    {"received_samples",counter(received.load())},{"processed_sample",counter(engine.processed_sample.load())},
    {"valid_frames",counter(engine.frames.load())},{"valid_fixes",counter(engine.fixes.load())},{"complete",true}};
  std::ofstream report(std::filesystem::path(directory)/"summary.json");report<<Json(summary).dump()<<'\n';report.close();
  if(!report)throw std::runtime_error("Cannot write summary");
  std::cout<<Json(summary).dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"BDS replay: "<<e.what()<<'\n';return 2;}}

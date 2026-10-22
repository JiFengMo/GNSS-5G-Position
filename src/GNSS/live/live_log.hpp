#pragma once
#include "live_json.hpp"
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace b1live {
inline int64_t wall_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
class Log {
  std::filesystem::path directory_;std::string id_;Object config_;std::ofstream file_;std::mutex mutex_;
  uint64_t seq_=0,segment_=0,bytes_=0;std::deque<std::filesystem::path> owned_;
  size_t limit_=32*1024*1024;
  void line(const std::string& kind,const Object& data){
    std::string text=Json(Object{{"schema",1},{"run_id",id_},{"seq",counter(seq_++)},{"time_ms",wall_ms()},{"kind",kind},{"data",data}}).dump()+"\n";
    file_<<text;bytes_+=text.size();if(!file_)throw std::runtime_error("B1C monitor write failed");
  }
  void rotate(){
    if(file_.is_open())file_.close();
    auto path=directory_/("events-"+std::to_string(segment_++)+".jsonl");
    file_.open(path,std::ios::binary|std::ios::out|std::ios::trunc);if(!file_)throw std::runtime_error("Cannot create B1C monitor segment");
    owned_.push_back(path);bytes_=0;line("config",config_);file_.flush();
    // These are only files created in the new directory owned by this Log.
    while(owned_.size()>8){std::filesystem::remove(owned_.front());owned_.pop_front();}
  }
public:
  Log(std::filesystem::path dir,std::string id,Object config):directory_(std::move(dir)),id_(std::move(id)),config_(std::move(config)){
    std::filesystem::create_directories(directory_.parent_path());
    if(!std::filesystem::create_directory(directory_))throw std::runtime_error("Live output must be a new directory");
    rotate();
  }
  void emit(const char* kind,const Object& data){std::lock_guard<std::mutex> lock(mutex_);if(bytes_>=limit_)rotate();line(kind,data);if(std::string(kind)=="runtime")file_.flush();}
};
}

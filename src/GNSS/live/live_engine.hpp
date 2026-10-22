#pragma once
#include "bds5g/b1c_position.h"
#include "live_json.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace b1live {
struct Block {uint64_t first=0;std::vector<bds5g_cf32_t> iq;bool discontinuous=false;};
// The receiver only moves buffers and counters under this short lock. No DSP,
// file IO, or waiting for free capacity occurs in the receive thread.
class Queue {
  size_t capacity_,samples_=0;std::deque<Block> blocks_;std::mutex mutex_;std::condition_variable cv_;bool closed_=false;
public:
  std::atomic<uint64_t> dropped_samples{0},overruns{0};
  explicit Queue(size_t capacity):capacity_(capacity){}
  void push(Block block);
  bool pop(Block& block);
  void close();
  size_t samples();
};
struct Options {
  double fs=4092000,if_hz=0,rf_hz=1575420000;
  double period_ms=200,elevation=5,offset_ms=68.802,max_age=7200;
  bool tropo=true;
  bds_b1c_acquisition_options_t acquisition{};
  bds_b1c_tracking_options_t tracking{};
  std::vector<int> prns;
  Options();
};
using Emit=std::function<void(const char*,const Object&)>;
class Engine {
  struct Impl;std::unique_ptr<Impl> p_;
public:
  Engine(Options options,Emit emit);~Engine();
  void push(const Block& block);
  // Also used by deterministic epoch-vector tests; same navigator/PVT path.
  void epoch(const bds_b1c_tracking_epoch_t& epoch);
  void finish_epochs();
  void gap(uint64_t sample,const char* reason);
  std::atomic<uint64_t> processed_sample{0},fixes{0},frames{0};
  std::atomic<unsigned> active{0};
  std::atomic<bool> searching{true};
};
Object tracking_json(const bds_b1c_tracking_epoch_t& e,double fs,uint64_t segment);
Object frame_json(const bds_b1c_nav_frame_t& f,double fs,uint64_t segment);
}

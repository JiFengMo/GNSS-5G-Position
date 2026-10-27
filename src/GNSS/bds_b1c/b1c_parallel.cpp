#include "bds5g/b1c_parallel.h"
#include "b1c_acquisition_internal.h"
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

struct bds_b1c_worker_pool {
  std::mutex mutex;
  std::condition_variable work, done;
  std::vector<std::thread> workers;
  bds_b1c_task_t task=nullptr;
  void* context=nullptr;
  size_t count=0, next=0, remaining=0;
  uint64_t generation=0;
  bool stopping=false, running=false;
  std::exception_ptr failure;

  explicit bds_b1c_worker_pool(unsigned threads) {
    if (threads==1) return;
    try {
      for (unsigned j=0;j<threads;++j) workers.emplace_back([this]{loop();});
    } catch (...) { shutdown(); throw; }
  }
  ~bds_b1c_worker_pool() { shutdown(); }
  void shutdown() noexcept {
    { std::lock_guard<std::mutex> lock(mutex); stopping=true; }
    work.notify_all();
    for (auto& worker:workers) if (worker.joinable()) worker.join();
  }
  void loop() {
    uint64_t seen=0;
    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
      work.wait(lock,[&]{return stopping||generation!=seen;});
      if (stopping) return;
      seen=generation;
      while (next<count) {
        const size_t index=next++;
        auto callback=task;void* user=context;
        lock.unlock();
        std::exception_ptr error;
        try {callback(index,user);} catch (...) {error=std::current_exception();}
        lock.lock();
        if (error&&!failure) failure=error;
      }
      if (--remaining==0) done.notify_one();
    }
  }
};

extern "C" int bds_b1c_worker_pool_create(unsigned threads,bds_b1c_worker_pool_t** out) {
  if (!out) return -1;
  *out=nullptr;
  if (!threads||threads>63) return -1;
  try {*out=new bds_b1c_worker_pool(threads);return 0;} catch (...) {return -2;}
}
extern "C" void bds_b1c_worker_pool_destroy(bds_b1c_worker_pool_t* pool) {delete pool;}
extern "C" int bds_b1c_worker_pool_run(bds_b1c_worker_pool_t* pool,size_t tasks,
                                        bds_b1c_task_t task,void* context) {
  if (!pool||!task) return -1;
  try {
    std::unique_lock<std::mutex> lock(pool->mutex);
    if (pool->running||pool->stopping) return -1;
    pool->running=true;
    if (pool->workers.empty()) {
      lock.unlock();
      std::exception_ptr failure;
      for (size_t j=0;j<tasks;++j) try {task(j,context);} catch (...) {if(!failure)failure=std::current_exception();}
      lock.lock();pool->running=false;
      return failure?-3:0;
    }
    pool->task=task;pool->context=context;pool->count=tasks;pool->next=0;
    pool->remaining=pool->workers.size();pool->failure=nullptr;++pool->generation;
    pool->work.notify_all();
    pool->done.wait(lock,[&]{return pool->remaining==0;});
    pool->task=nullptr;pool->context=nullptr;pool->running=false;
    return pool->failure?-3:0;
  } catch (...) {return -2;}
}

// All FFTW planning/destruction in bds_b1c uses this lock, including the
// legacy serial API. No process-wide fftw_cleanup() or FFTW thread setting.
static std::mutex planner_mutex;
extern "C" void b1c_fftw_planner_lock(void) {planner_mutex.lock();}
extern "C" void b1c_fftw_planner_unlock(void) {planner_mutex.unlock();}

extern "C" int bds_b1c_acquire_many_parallel(const bds5g_cf32_t* samples,size_t count,
    const bds_b1c_acquisition_options_t* options,const int* prns,size_t prn_count,
    bds_b1c_acquisition_t* results,unsigned threads) {
  if (!threads||threads>63) return -1;
  b1c_acquisition_batch_t* raw=nullptr;
  int status=b1c_acquisition_prepare(samples,count,options,prns,prn_count,results,threads,&raw);
  std::unique_ptr<b1c_acquisition_batch_t,decltype(&b1c_acquisition_destroy)> batch(raw,b1c_acquisition_destroy);
  if (status||!raw) return status;
  try {
    unsigned workers=b1c_acquisition_workers(raw);
    bds_b1c_worker_pool_t* raw_pool=nullptr;
    if (bds_b1c_worker_pool_create(workers,&raw_pool)) return -2;
    std::unique_ptr<bds_b1c_worker_pool_t,decltype(&bds_b1c_worker_pool_destroy)> pool(raw_pool,bds_b1c_worker_pool_destroy);
    struct Tasks {b1c_acquisition_batch_t* batch;std::vector<int> status;} tasks{raw,std::vector<int>(workers)};
    auto run=[](size_t index,void* context){auto& t=*static_cast<Tasks*>(context);t.status[index]=b1c_acquisition_run(t.batch,unsigned(index));};
    if (bds_b1c_worker_pool_run(pool.get(),workers,run,&tasks)) return -2;
    for (int error:tasks.status) if (error) return error;
    return 0;
  } catch (...) {return -2;}
}

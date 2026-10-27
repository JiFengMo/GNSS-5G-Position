#pragma once
#include "bds5g/b1c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A bounded, persistent pool owned by one caller. run() is synchronous;
 * callbacks own disjoint state and must not reenter/destroy this same pool.
 * Every submitted task finishes before run() returns, even on an exception.
 * One thread executes directly without creating a background worker.
 * Return 0 on success, -1 invalid arguments, -2 allocation/thread failure,
 * -3 a C++ task exception. No exception crosses the C ABI. */
typedef struct bds_b1c_worker_pool bds_b1c_worker_pool_t;
typedef void (*bds_b1c_task_t)(size_t index, void *context);
int bds_b1c_worker_pool_create(unsigned threads, bds_b1c_worker_pool_t **pool);
int bds_b1c_worker_pool_run(bds_b1c_worker_pool_t *pool, size_t tasks,
                            bds_b1c_task_t task, void *context);
void bds_b1c_worker_pool_destroy(bds_b1c_worker_pool_t *pool);

/* Same numeric kernel, PRN ordering and error codes as acquire_many().
 * 1..63 threads, capped by PRN count and a 512 MiB FFT-workspace budget.
 * Independent per-call state; no global signal/code cache. FFTW's planner
 * is serialized across all acquisition calls in this library. */
int bds_b1c_acquire_many_parallel(const bds5g_cf32_t *samples, size_t sample_count,
                                  const bds_b1c_acquisition_options_t *options,
                                  const int *prns, size_t prn_count,
                                  bds_b1c_acquisition_t *results, unsigned threads);

#ifdef __cplusplus
}
#endif

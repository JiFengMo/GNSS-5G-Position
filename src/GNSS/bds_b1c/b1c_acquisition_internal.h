#pragma once
#include "bds5g/b1c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct b1c_acquisition_batch b1c_acquisition_batch_t;
int b1c_acquisition_prepare(const bds5g_cf32_t *samples, size_t sample_count,
                            const bds_b1c_acquisition_options_t *options,
                            const int *prns, size_t prn_count,
                            bds_b1c_acquisition_t *results, unsigned workers,
                            b1c_acquisition_batch_t **batch);
unsigned b1c_acquisition_workers(const b1c_acquisition_batch_t *batch);
int b1c_acquisition_run(b1c_acquisition_batch_t *batch, unsigned worker);
void b1c_acquisition_destroy(b1c_acquisition_batch_t *batch);
/* FFTW has one process-wide planner. Execution uses independent plans. */
void b1c_fftw_planner_lock(void);
void b1c_fftw_planner_unlock(void);

#ifdef __cplusplus
}
#endif

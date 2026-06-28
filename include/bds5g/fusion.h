#pragma once

#include "bds5g/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  unsigned max_iterations;
  double position_tolerance_m;
  double huber_threshold_sigma;
} bds5g_wls_options_t;

bds5g_wls_options_t bds5g_wls_default_options(void);

int bds5g_fusion_solve_wls(const bds5g_observation_t *observations,
                           size_t observation_count,
                           bds5g_vec3_t initial_ecef_m,
                           const bds5g_wls_options_t *options,
                           bds5g_solution_t *solution);

#ifdef __cplusplus
}
#endif


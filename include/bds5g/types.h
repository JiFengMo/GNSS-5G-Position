#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BDS5G_MAX_OBSERVATIONS 64U
#define BDS5G_SPEED_OF_LIGHT_MPS 299792458.0

typedef struct {
  double x;
  double y;
  double z;
} bds5g_vec3_t;

typedef struct {
  float i;
  float q;
} bds5g_cf32_t;

typedef enum {
  BDS5G_SOURCE_BDS_B1C = 0,
  BDS5G_SOURCE_NR_SSB = 1,
  BDS5G_SOURCE_NR_PRS = 2
} bds5g_source_t;

typedef struct {
  uint64_t timestamp_ns;
  bds5g_source_t source;
  int source_id;
  bds5g_vec3_t source_ecef_m;
  double pseudorange_m;
  double sigma_m;
  double cn0_db_hz;
  uint8_t valid;
} bds5g_observation_t;

typedef struct {
  bds5g_vec3_t receiver_ecef_m;
  double bds_clock_bias_m;
  double nr_clock_bias_m;
  double residual_rms_m;
  size_t used_observations;
  uint8_t converged;
} bds5g_solution_t;

#ifdef __cplusplus
}
#endif


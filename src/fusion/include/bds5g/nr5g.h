#pragma once

#include "bds5g/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int anchor_id;
  int pci;
  bds5g_vec3_t ecef_m;
  double calibrated_delay_ns;
  uint8_t absolute_time_valid;
} bds5g_nr_anchor_t;

typedef struct {
  uint64_t timestamp_ns;
  int pci;
  double toa_ns;
  double snr_db;
  double peak_metric;
  uint8_t valid;
} bds5g_nr_toa_t;

int bds5g_nr_to_observation(const bds5g_nr_anchor_t *anchor,
                            const bds5g_nr_toa_t *measurement,
                            bds5g_observation_t *observation);

#ifdef __cplusplus
}
#endif


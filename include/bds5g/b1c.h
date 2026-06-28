#pragma once

#include "bds5g/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BDS_B1C_PRIMARY_CODE_LENGTH 10230U

typedef struct {
  int prn;
  double doppler_hz;
  size_t code_phase_samples;
  double peak_metric;
  double cn0_db_hz;
  uint8_t acquired;
} bds_b1c_acquisition_t;

typedef struct {
  double carrier_doppler_hz;
  double carrier_phase_rad;
  double code_phase_chips;
  double code_rate_hz;
  double dll_error;
  double pll_error;
  uint32_t coherent_blocks;
  uint8_t locked;
} bds_b1c_tracking_t;

typedef struct {
  int prn;
  uint32_t week;
  double tow_s;
  double sqrt_a;
  double eccentricity;
  double inclination_rad;
  double omega0_rad;
  double argument_of_perigee_rad;
  double mean_anomaly_rad;
  double clock_bias_s;
  double clock_drift_sps;
  uint8_t valid;
} bds_b1c_ephemeris_t;

int bds_b1c_generate_primary_code(int prn, int8_t *chips, size_t chip_count);

int bds_b1c_acquire(const bds5g_cf32_t *samples,
                    size_t sample_count,
                    double sample_rate_hz,
                    int prn,
                    double doppler_min_hz,
                    double doppler_max_hz,
                    double doppler_step_hz,
                    bds_b1c_acquisition_t *result);

void bds_b1c_tracking_init(const bds_b1c_acquisition_t *acquisition,
                           bds_b1c_tracking_t *tracking);

int bds_b1c_tracking_update(bds_b1c_tracking_t *tracking,
                            bds5g_cf32_t early,
                            bds5g_cf32_t prompt,
                            bds5g_cf32_t late,
                            double integration_time_s);

int bds_b1c_decode_navigation(const uint8_t *bits,
                              size_t bit_count,
                              int prn,
                              bds_b1c_ephemeris_t *ephemeris);

#ifdef __cplusplus
}
#endif

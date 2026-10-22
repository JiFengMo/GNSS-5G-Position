#pragma once

#include "bds5g/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BDS_B1C_PRIMARY_CODE_LENGTH 10230U
#define BDS_B1C_SECONDARY_CODE_LENGTH 1800U
#define BDS_B1C_CHIP_RATE_HZ 1023000.0

typedef enum {
  BDS_B1C_DATA = 0,
  BDS_B1C_PILOT = 1
} bds_b1c_component_t;

typedef struct {
  double sample_rate_hz;
  double if_hz;                   /* Carrier frequency = IF + Doppler. */
  double doppler_min_hz;
  double doppler_max_hz;
  double doppler_step_hz;
  double fine_step_hz;
  double coherent_integration_ms; /* 1..10 ms; input must span 20 ms. */
  double threshold;               /* CU-SDR amplitude/variance statistic. */
} bds_b1c_acquisition_options_t;

typedef struct {
  int prn;
  double doppler_hz;
  size_t code_phase_samples;
  double peak_metric;
  double cn0_db_hz;
  uint8_t acquired;
  double carrier_hz;
  double coarse_doppler_hz;
  double fine_metric;
} bds_b1c_acquisition_t;

typedef struct {
  /* MATLAB names; orbit m/s/rad, clock seconds, BDT week/TOW.
   * Ionosphere coefficients use the ICD model units and scaling.
   * deltaA is relative to the ICD reference orbit, NOT sqrt(A).
   * Optional Page 1/3 fields belong to this frame; absent values are NaN. */
  int PRN;
  unsigned WN, HOW, SOH, IODC, IODE, SatType, PageID;
  unsigned HS, DIF, SIF, AIF, SISMAI;
  double TOW, t_oe, deltaA, ADot, delta_n_0, delta_n_0Dot;
  double M_0, e, omega, omega_0, i_0, omegaDot, i_0Dot;
  double C_is, C_ic, C_rs, C_rc, C_us, C_uc;
  double t_oc, a_0, a_1, a_2, T_GDB2ap, ISC_B1Cd, T_GDB1Cp;
  double alpha1, alpha2, alpha3, alpha4, alpha5, alpha6, alpha7, alpha8, alpha9;
  double A_0UTC, A_1UTC, A_2UTC, delta_t_LS, t_ot, WN_ot, WN_LSF, DN, delta_t_LSF;
  double GNSS_ID, WN_0BGTO, t_0BGTO, A_0BGTO, A_1BGTO, A_2BGTO;
  uint8_t valid; /* CRC + identity + basic ranges; NOT positioning fitness. */
} bds_b1c_ephemeris_t;

int bds_b1c_generate_primary_code(int prn, int8_t *chips, size_t chip_count);
/* ICD bipolar mapping: bit 0 -> +1, bit 1 -> -1. Legacy API means data. */
int bds_b1c_generate_code(int prn, bds_b1c_component_t component,
                          int8_t *chips, size_t chip_count);
int bds_b1c_generate_secondary_code(int prn, int8_t *chips, size_t chip_count);
/* Narrowband BOC(1,1) replica, not a full QMBOC pilot replica.
 * Right-edge sampling follows CU-SDR makeDataTable/makePilotTable, including
 * their floating-point ceil convention. Writes round(fs * .01) samples. */
int bds_b1c_sample_boc11(int prn, bds_b1c_component_t component,
                         double sample_rate_hz, int8_t *samples, size_t count);
void bds_b1c_acquisition_defaults(bds_b1c_acquisition_options_t *options);
/* All PRNs share one FFT workspace. Requires 20 ms; coarse uses 10+T ms,
 * fine search uses a contiguous 10 ms block beginning at the acquired phase.
 * Phase is zero-based within one 10 ms period; it is not absolute TOA.
 * cn0_db_hz is NaN: the acquisition statistic is not a calibrated C/N0.
 * Returns 0 (also on no detections), -1 invalid input/options, -2 allocation
 * or FFT planning failure. FFTW planning calls must be externally serialized. */
int bds_b1c_acquire_many(const bds5g_cf32_t *samples, size_t sample_count,
                         const bds_b1c_acquisition_options_t *options,
                         const int *prns, size_t prn_count,
                         bds_b1c_acquisition_t *results);

int bds_b1c_acquire(const bds5g_cf32_t *samples,
                    size_t sample_count,
                    double sample_rate_hz,
                    int prn,
                    double doppler_min_hz,
                    double doppler_max_hz,
                    double doppler_step_hz,
                    bds_b1c_acquisition_t *result);

/* Parse exactly 878 decoded information/CRC bits, MSB first, bytes 0 or 1.
 * Checks both CRC24Q blocks and PRN; zeroes output on failure.
 * Returns 0 success, -1 arguments/bits, -2 CRC, -3 identity/field ranges.
 * Tracking: b1c_tracking.h; frame/stream decoding: b1c_navigation.h. */
int bds_b1c_decode_navigation(const uint8_t *bits,
                              size_t bit_count,
                              int prn,
                              bds_b1c_ephemeris_t *ephemeris);

#ifdef __cplusplus
}
#endif

#include "bds5g/b1c.h"

#include <math.h>
#include <string.h>

static uint32_t read_unsigned(const uint8_t *bits, size_t start, size_t width)
{
  uint32_t value = 0U;
  size_t i;
  for (i = 0U; i < width; ++i) {
    value = (value << 1U) | (uint32_t)(bits[start + i] & 1U);
  }
  return value;
}

int bds_b1c_decode_navigation(const uint8_t *bits,
                              size_t bit_count,
                              int prn,
                              bds_b1c_ephemeris_t *ephemeris)
{
  uint32_t sync;
  if (!bits || !ephemeris || bit_count < 192U || prn < 1 || prn > 63) {
    return -1;
  }
  memset(ephemeris, 0, sizeof(*ephemeris));
  sync = read_unsigned(bits, 0U, 16U);
  if (sync != 0xEB90U) {
    return -2;
  }

  ephemeris->prn = prn;
  ephemeris->week = read_unsigned(bits, 16U, 13U);
  ephemeris->tow_s = (double)read_unsigned(bits, 29U, 20U) * 6.0;
  ephemeris->sqrt_a = 5000.0 + (double)read_unsigned(bits, 49U, 24U) / 4096.0;
  ephemeris->eccentricity = (double)read_unsigned(bits, 73U, 24U) / 16777216.0;
  ephemeris->inclination_rad =
      ((double)read_unsigned(bits, 97U, 20U) / 1048576.0) * 3.141592653589793;
  ephemeris->omega0_rad =
      ((double)read_unsigned(bits, 117U, 20U) / 524288.0 - 1.0) *
      3.141592653589793;
  ephemeris->argument_of_perigee_rad =
      ((double)read_unsigned(bits, 137U, 20U) / 524288.0 - 1.0) *
      3.141592653589793;
  ephemeris->mean_anomaly_rad =
      ((double)read_unsigned(bits, 157U, 20U) / 524288.0 - 1.0) *
      3.141592653589793;
  ephemeris->clock_bias_s =
      ((double)read_unsigned(bits, 177U, 15U) - 16384.0) * 1.0e-9;
  ephemeris->clock_drift_sps = 0.0;
  ephemeris->valid = (ephemeris->sqrt_a > 5000.0 &&
                      ephemeris->sqrt_a < 6000.0 &&
                      ephemeris->eccentricity < 0.1) ? 1U : 0U;
  return ephemeris->valid ? 0 : -3;
}

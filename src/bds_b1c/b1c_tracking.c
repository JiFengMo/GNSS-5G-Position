#include "bds5g/b1c.h"

#include <math.h>
#include <string.h>

#define B1C_NOMINAL_CODE_RATE_HZ 1023000.0
#define B1C_PI 3.14159265358979323846

static double magnitude(bds5g_cf32_t value)
{
  return sqrt((double)value.i * value.i + (double)value.q * value.q);
}

void bds_b1c_tracking_init(const bds_b1c_acquisition_t *acquisition,
                           bds_b1c_tracking_t *tracking)
{
  if (!tracking) {
    return;
  }
  memset(tracking, 0, sizeof(*tracking));
  tracking->code_rate_hz = B1C_NOMINAL_CODE_RATE_HZ;
  if (acquisition) {
    tracking->carrier_doppler_hz = acquisition->doppler_hz;
    tracking->code_phase_chips = (double)acquisition->code_phase_samples;
    tracking->locked = acquisition->acquired;
  }
}

int bds_b1c_tracking_update(bds_b1c_tracking_t *tracking,
                            bds5g_cf32_t early,
                            bds5g_cf32_t prompt,
                            bds5g_cf32_t late,
                            double integration_time_s)
{
  const double early_mag = magnitude(early);
  const double late_mag = magnitude(late);
  const double prompt_mag = magnitude(prompt);
  double phase_error;

  if (!tracking || integration_time_s <= 0.0) {
    return -1;
  }

  tracking->dll_error = (early_mag - late_mag) /
                        (early_mag + late_mag + 1.0e-12);
  phase_error = atan2((double)prompt.q, (double)prompt.i);
  tracking->pll_error = phase_error / (2.0 * B1C_PI);

  /* Conservative second-order-like loop update for offline replay. */
  tracking->code_rate_hz += 40.0 * tracking->dll_error;
  tracking->code_phase_chips += tracking->code_rate_hz * integration_time_s;
  tracking->carrier_doppler_hz += 12.0 * tracking->pll_error;
  tracking->carrier_phase_rad +=
      2.0 * B1C_PI * tracking->carrier_doppler_hz * integration_time_s;
  tracking->coherent_blocks += 1U;
  tracking->locked = (prompt_mag > 1.0e-6 &&
                      fabs(tracking->dll_error) < 0.8 &&
                      fabs(tracking->pll_error) < 0.45) ? 1U : 0U;
  return 0;
}


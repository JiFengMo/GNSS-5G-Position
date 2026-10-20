#include "bds5g/b1c.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define B1C_CHIP_RATE_HZ 1023000.0
#define B1C_PI 3.14159265358979323846

int bds_b1c_acquire(const bds5g_cf32_t *samples,
                    size_t sample_count,
                    double sample_rate_hz,
                    int prn,
                    double doppler_min_hz,
                    double doppler_max_hz,
                    double doppler_step_hz,
                    bds_b1c_acquisition_t *result)
{
  int8_t code[BDS_B1C_PRIMARY_CODE_LENGTH];
  size_t phase_step;
  size_t phase;
  size_t correlation_samples;
  double doppler;
  double best_power = 0.0;
  double accumulated_power = 0.0;
  size_t candidates = 0U;

  if (!samples || !result || sample_count < 32U || sample_rate_hz <= 0.0 ||
      doppler_step_hz <= 0.0 || doppler_max_hz < doppler_min_hz) {
    return -1;
  }
  memset(result, 0, sizeof(*result));
  result->prn = prn;
  if (bds_b1c_generate_primary_code(prn, code, sizeof(code)) != 0) {
    return -1;
  }

  correlation_samples = sample_count;
  if (correlation_samples > 8192U) {
    correlation_samples = 8192U;
  }
  phase_step = (size_t)floor(sample_rate_hz / (2.0 * B1C_CHIP_RATE_HZ));
  if (phase_step < 1U) {
    phase_step = 1U;
  }

  for (doppler = doppler_min_hz;
       doppler <= doppler_max_hz + 0.5 * doppler_step_hz;
       doppler += doppler_step_hz) {
    for (phase = 0U; phase < sample_count && phase < 4096U; phase += phase_step) {
      double corr_i = 0.0;
      double corr_q = 0.0;
      size_t n;
      for (n = 0U; n < correlation_samples; ++n) {
        const size_t sample_index = (n + phase) % sample_count;
        const size_t chip_index = (size_t)floor((double)n * B1C_CHIP_RATE_HZ /
                                                sample_rate_hz) %
                                  BDS_B1C_PRIMARY_CODE_LENGTH;
        const double angle = 2.0 * B1C_PI * doppler * (double)n / sample_rate_hz;
        const double cosine = cos(angle);
        const double sine = sin(angle);
        const double mixed_i = samples[sample_index].i * cosine +
                               samples[sample_index].q * sine;
        const double mixed_q = samples[sample_index].q * cosine -
                               samples[sample_index].i * sine;
        corr_i += mixed_i * code[chip_index];
        corr_q += mixed_q * code[chip_index];
      }
      {
        const double power = corr_i * corr_i + corr_q * corr_q;
        accumulated_power += power;
        ++candidates;
        if (power > best_power) {
          best_power = power;
          result->doppler_hz = doppler;
          result->code_phase_samples = phase;
        }
      }
    }
  }

  if (candidates == 0U) {
    return -1;
  }
  result->peak_metric = best_power /
                        (accumulated_power / (double)candidates + 1.0e-12);
  result->cn0_db_hz = 10.0 * log10(result->peak_metric + 1.0e-12) +
                      10.0 * log10(1.0 / ((double)correlation_samples /
                                          sample_rate_hz));
  result->acquired = (result->peak_metric >= 6.0) ? 1U : 0U;
  return 0;
}


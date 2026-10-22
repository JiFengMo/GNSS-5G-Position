/* SPDX-License-Identifier: GPL-2.0-or-later
 * C adaptation of the CU-SDR B1C acquisition method by Yafeng Li,
 * Nagaraj C. Shivaramaiah and Dennis M. Akos (GPL-2.0-or-later).
 * Li et al., GPS Solutions 23:60 (2019), doi:10.1007/s10291-019-0853-z.
 * Reference conventions and limitations: ../docs/offline_acquisition.md.
 */
#include "bds5g/b1c.h"

#include <fftw3.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define B1C_PI 3.14159265358979323846

void bds_b1c_acquisition_defaults(bds_b1c_acquisition_options_t *o)
{
  if (!o) return;
  *o = (bds_b1c_acquisition_options_t){
    .sample_rate_hz = 4092000.0, .if_hz = 0.0,
    .doppler_min_hz = -5000.0, .doppler_max_hz = 5000.0,
    .doppler_step_hz = 50.0, .fine_step_hz = 25.0,
    .coherent_integration_ms = 10.0, .threshold = 10.0
  };
}

static int valid_options(const bds_b1c_acquisition_options_t *o)
{
  if (!o || !isfinite(o->sample_rate_hz) || !isfinite(o->if_hz) ||
      !isfinite(o->doppler_min_hz) || !isfinite(o->doppler_max_hz) ||
      !isfinite(o->doppler_step_hz) || !isfinite(o->fine_step_hz) ||
      !isfinite(o->coherent_integration_ms) || !isfinite(o->threshold)) return 0;
  if (o->sample_rate_hz < 2.0 * BDS_B1C_CHIP_RATE_HZ || o->sample_rate_hz > 1e8 ||
      o->coherent_integration_ms < 1.0 || o->coherent_integration_ms > 10.0 ||
      o->doppler_step_hz <= 0 || o->fine_step_hz <= 0 || o->threshold <= 0 ||
      o->doppler_max_hz < o->doppler_min_hz) return 0;
  const double bins = (o->doppler_max_hz - o->doppler_min_hz) / o->doppler_step_hz;
  const double fine = o->doppler_step_hz / o->fine_step_hz;
  /* Explicit grids, inclusive endpoints, with the coarse centre on the fine grid. */
  if (!isfinite(bins) || bins > 20000 || fabs(bins - round(bins)) > 1e-7 ||
      !isfinite(fine) || fine < 1 || fine > 1000 || fabs(fine - round(fine)) > 1e-7)
    return 0;
  if (fabs(o->sample_rate_hz * .01 - round(o->sample_rate_hz * .01)) > 1e-6) return 0;
  return fabs(o->if_hz + o->doppler_min_hz) + o->doppler_step_hz < o->sample_rate_hz / 2 &&
         fabs(o->if_hz + o->doppler_max_hz) + o->doppler_step_hz < o->sample_rate_hz / 2;
}

static void mix(const bds5g_cf32_t *samples, size_t n, double fs, double frequency,
                fftw_complex *out)
{
  for (size_t k = 0; k < n; ++k) {
    const double phase = 2.0 * B1C_PI * frequency * (double)k / fs;
    const double c = cos(phase), s = sin(phase);
    out[k][0] = samples[k].i * c + samples[k].q * s;
    out[k][1] = samples[k].q * c - samples[k].i * s;
  }
}

static void product(const fftw_complex *signal, const fftw_complex *code,
                    fftw_complex *out, size_t n, size_t shift)
{
  size_t j = shift ? n - shift : 0;
  for (size_t k = 0; k < n; ++k) {
    const double ar = signal[j][0], ai = signal[j][1];
    const double br = code[k][0], bi = code[k][1];
    out[k][0] = ar * br + ai * bi;
    out[k][1] = ai * br - ar * bi;
    if (++j == n) j = 0;
  }
}

static double amplitude(double i, double q)
{
  /* Inputs originate in finite float32 IQ and an FFT of at most 2e6
   * samples. All intermediate squared magnitudes fit safely in double. */
  return sqrt(i * i + q * q);
}

int bds_b1c_acquire_many(const bds5g_cf32_t *samples, size_t sample_count,
                         const bds_b1c_acquisition_options_t *o,
                         const int *prns, size_t prn_count,
                         bds_b1c_acquisition_t *results)
{
  if (!samples || !prns || !results || !prn_count || prn_count > 63 || !valid_options(o)) return -1;
  for (size_t p = 0; p < prn_count; ++p) {
    if (prns[p] < 1 || prns[p] > 63) return -1;
    for (size_t q = 0; q < p; ++q) if (prns[p] == prns[q]) return -1;
  }
  const size_t period = (size_t)llround(o->sample_rate_hz * .01);
  const size_t coherent = (size_t)llround((double)period / 10.0 * o->coherent_integration_ms);
  const size_t length = (size_t)llround((double)period / 10.0 * (10.0 + o->coherent_integration_ms));
  if (sample_count < 2U * period || length > INT_MAX || coherent < 2) return -1;
  double sum_i = 0, sum_q = 0;
  for (size_t k = 0; k < 2U * period; ++k) {
    if (!isfinite(samples[k].i) || !isfinite(samples[k].q)) return -1;
    if (k < coherent) { sum_i += samples[k].i; sum_q += samples[k].q; }
  }
  const double mean_i = sum_i / coherent, mean_q = sum_q / coherent;
  double variance_sum = 0;
  for (size_t k = 0; k < coherent; ++k) {
    const double di = samples[k].i - mean_i, dq = samples[k].q - mean_q;
    variance_sum += di * di + dq * dq;
  }
  const double normalization = sqrt(variance_sum / (coherent - 1U) * coherent);
  for (size_t p = 0; p < prn_count; ++p) {
    memset(&results[p], 0, sizeof(results[p]));
    results[p].prn = prns[p]; results[p].cn0_db_hz = NAN;
    results[p].carrier_hz = NAN; results[p].doppler_hz = NAN;
    results[p].coarse_doppler_hz = NAN;
  }
  if (normalization == 0) return 0;

  int status = -2;
  fftw_complex *work = fftw_alloc_complex(length);
  fftw_complex *signal = fftw_alloc_complex(length);
  fftw_complex *data_fft = fftw_alloc_complex(length);
  fftw_complex *pilot_fft = fftw_alloc_complex(length);
  double *data_magnitude = malloc(length * sizeof(*data_magnitude));
  int8_t *data = malloc(period), *pilot = malloc(period);
  fftw_plan forward = NULL, inverse = NULL;
  if (!work || !signal || !data_fft || !pilot_fft || !data_magnitude || !data || !pilot) goto done;
  forward = fftw_plan_dft_1d((int)length, work, work, FFTW_FORWARD, FFTW_ESTIMATE);
  inverse = fftw_plan_dft_1d((int)length, work, work, FFTW_BACKWARD, FFTW_ESTIMATE);
  if (!forward || !inverse) goto done;

  const double fft_bins_per_step = o->doppler_step_hz * length / o->sample_rate_hz;
  const int shifted_grid = fabs(fft_bins_per_step - round(fft_bins_per_step)) < 1e-8;
  const size_t frequency_count = (size_t)llround((o->doppler_max_hz - o->doppler_min_hz) /
                                               o->doppler_step_hz) + 1U;
  if (shifted_grid) {
    mix(samples, length, o->sample_rate_hz, o->if_hz + o->doppler_max_hz, work);
    fftw_execute(forward);
    memcpy(signal, work, length * sizeof(*signal));
  }
  for (size_t p = 0; p < prn_count; ++p) {
    bds_b1c_acquisition_t *r = &results[p];
    if (bds_b1c_sample_boc11(prns[p], BDS_B1C_DATA, o->sample_rate_hz, data, period) ||
        bds_b1c_sample_boc11(prns[p], BDS_B1C_PILOT, o->sample_rate_hz, pilot, period)) {
      status = -1; goto done;
    }
    for (size_t k = 0; k < length; ++k) {
      work[k][0] = k < coherent ? data[k] : 0; work[k][1] = 0;
    }
    fftw_execute(forward); memcpy(data_fft, work, length * sizeof(*work));
    for (size_t k = 0; k < length; ++k) {
      work[k][0] = k < coherent ? pilot[k] : 0; work[k][1] = 0;
    }
    fftw_execute(forward); memcpy(pilot_fft, work, length * sizeof(*work));

    double best = -1, selected_doppler = o->doppler_max_hz;
    size_t selected_phase = 0;
    for (size_t bin = 0; bin < frequency_count; ++bin) {
      const double doppler = o->doppler_max_hz - bin * o->doppler_step_hz;
      size_t shift = 0;
      if (shifted_grid) shift = (size_t)llround(bin * fft_bins_per_step) % length;
      else {
        mix(samples, length, o->sample_rate_hz, o->if_hz + doppler, work);
        fftw_execute(forward); memcpy(signal, work, length * sizeof(*signal));
      }
      product(signal, data_fft, work, length, shift); fftw_execute(inverse);
      for (size_t k = 0; k < length; ++k)
        data_magnitude[k] = amplitude(work[k][0], work[k][1]) / length;
      product(signal, pilot_fft, work, length, shift); fftw_execute(inverse);
      for (size_t k = 0; k < length; ++k) {
        const double value = (sqrt(11.0) * data_magnitude[k] +
                             sqrt(29.0) * amplitude(work[k][0], work[k][1]) / length) / sqrt(40.0);
        if (value > best) { best = value; selected_phase = k; selected_doppler = doppler; }
      }
    }
    /* CU-SDR searches all circular lags of its zero-padded 10+T ms FFT.
     * Fold the winning lag to one code period, then refine on contiguous IQ. */
    r->code_phase_samples = selected_phase % period;
    r->peak_metric = best / normalization;
    r->coarse_doppler_hz = selected_doppler;
    r->acquired = r->peak_metric > o->threshold;
    if (!r->acquired) continue;
    const size_t fine_count = (size_t)llround(2 * o->doppler_step_hz / o->fine_step_hz) + 1U;
    double best_fine = -1;
    for (size_t bin = 0; bin < fine_count; ++bin) {
      const double doppler = selected_doppler + o->doppler_step_hz - bin * o->fine_step_hz;
      double di = 0, dq = 0, pi = 0, pq = 0;
      for (size_t k = 0; k < period; ++k) {
        const bds5g_cf32_t x = samples[r->code_phase_samples + k];
        const double phase = 2 * B1C_PI * (o->if_hz + doppler) * k / o->sample_rate_hz;
        const double c = cos(phase), s = sin(phase);
        const double i = x.i * c + x.q * s, q = x.q * c - x.i * s;
        di += i * data[k]; dq += q * data[k]; pi += i * pilot[k]; pq += q * pilot[k];
      }
      const double value = (11 * amplitude(di, dq) + 29 * amplitude(pi, pq)) / 40;
      if (value > best_fine) { best_fine = value; r->doppler_hz = doppler; }
    }
    r->carrier_hz = o->if_hz + r->doppler_hz;
    r->fine_metric = best_fine;
  }
  status = 0;
done:
  if (forward) fftw_destroy_plan(forward);
  if (inverse) fftw_destroy_plan(inverse);
  fftw_free(work); fftw_free(signal); fftw_free(data_fft); fftw_free(pilot_fft);
  free(data_magnitude); free(data); free(pilot);
  return status;
}

int bds_b1c_acquire(const bds5g_cf32_t *samples, size_t sample_count, double fs,
                    int prn, double lower, double upper, double step,
                    bds_b1c_acquisition_t *result)
{
  bds_b1c_acquisition_options_t o;
  bds_b1c_acquisition_defaults(&o);
  o.sample_rate_hz = fs; o.doppler_min_hz = lower;
  o.doppler_max_hz = upper; o.doppler_step_hz = step;
  o.fine_step_hz = fmin(25.0, step);
  return bds_b1c_acquire_many(samples, sample_count, &o, &prn, 1, result);
}

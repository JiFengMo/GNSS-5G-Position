#include "nr_pos_api.h"

#include <limits.h>
#include <math.h>
#include <string.h>

int nr_toa_find_integer_peak(const nr_cir_t *cir, int *peak_idx)
{
  double best_mag = 0.0;
  if (!cir || !peak_idx || !cir->cir || !cir->cir_len || cir->cir_len > INT_MAX) {
    return -1;
  }
  *peak_idx = 0;
  for (uint32_t n = 0U; n < cir->cir_len; n++) {
    const double mag = hypot(cir->cir[n].r, cir->cir[n].i);
    if (!isfinite(mag)) return -1;
    if (mag > best_mag) {
      best_mag = mag;
      *peak_idx = (int)n;
    }
  }
  return best_mag > 0.0 ? 0 : -1;
}

int nr_toa_refine_fractional(const nr_cir_t *cir, int peak_idx, double *frac)
{
  double yl, yc, yr, den;
  uint32_t peak, left, right;
  if (!cir || !frac || !cir->cir || cir->cir_len < 3U ||
      peak_idx < 0 || (uint32_t)peak_idx >= cir->cir_len) return -1;
  *frac = 0.0;
  peak = (uint32_t)peak_idx;
  left = peak ? peak - 1U : cir->cir_len - 1U;
  right = peak + 1U == cir->cir_len ? 0U : peak + 1U;
  yl = hypot(cir->cir[left].r, cir->cir[left].i);
  yc = hypot(cir->cir[peak].r, cir->cir[peak].i);
  yr = hypot(cir->cir[right].r, cir->cir[right].i);
  if (!isfinite(yl) || !isfinite(yc) || !isfinite(yr) || yc <= 0.0 ||
      yc < yl || yc < yr) return -1;
  den = yl - 2.0 * yc + yr;
  if (fabs(den) <= 1.0e-12 * yc) return 0;
  *frac = fmax(-0.5, fmin(0.5, 0.5 * (yl - yr) / den));
  return 0;
}

int nr_toa_cir_delay_samples(const nr_cir_t *cir, int peak_idx, double frac,
                             double fs_hz, double *delay_samp)
{
  double bin;
  if (!cir || !delay_samp || !cir->cir_len || peak_idx < 0 ||
      (uint32_t)peak_idx >= cir->cir_len || !isfinite(frac) || fabs(frac) > 0.5 ||
      !isfinite(cir->bin_width_s) || cir->bin_width_s <= 0.0 ||
      !isfinite(fs_hz) || fs_hz <= 0.0) return -1;
  /* IDFT indices are signed residuals about the FFT window. Unwrap after
   * interpolation, including fractions crossing index zero. */
  bin = peak_idx + frac;
  if (bin >= 0.5 * cir->cir_len) bin -= cir->cir_len;
  if (bin < -0.5 * cir->cir_len) bin += cir->cir_len;
  *delay_samp = bin * cir->bin_width_s * fs_hz;
  return isfinite(*delay_samp) ? 0 : -1;
}

static int add_signed(int64_t a, int64_t b, int64_t *result)
{
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return -1;
  *result = a + b;
  return 0;
}

int nr_toa_build_meas(const nr_sync_state_t *sync, const nr_iq_block_t *blk,
                      double residual_delay_samp, nr_toa_meas_t *meas)
{
  int64_t offset;
  uint64_t rx_int, magnitude;
  double whole;
  nr_toa_meas_t result;
  if (!meas) return -1;
  memset(meas, 0, sizeof(*meas));
  if (!sync || !blk || !isfinite(blk->fs_hz) || blk->fs_hz <= 0.0 ||
      !isfinite(residual_delay_samp)) return -1;
  whole = floor(residual_delay_samp);
  if (whole < -0x1p63 || whole >= 0x1p63 ||
      add_signed(sync->cum_tracking_shift_samp, sync->coarse_offset_samp, &offset) ||
      add_signed(offset, (int64_t)whole, &offset)) return -1;

  /* Keep hardware counters integer, including values exceeding 2^53. */
  if (offset < 0) {
    magnitude = (uint64_t)(-(offset + 1)) + 1U;
    if (blk->ts_first < magnitude) return -1;
    rx_int = blk->ts_first - magnitude;
  } else {
    if (blk->ts_first > UINT64_MAX - (uint64_t)offset) return -1;
    rx_int = blk->ts_first + (uint64_t)offset;
  }
  memset(&result, 0, sizeof(result));
  result.rx_ts_int = rx_int;
  result.rx_ts_frac = residual_delay_samp - whole; /* Normalized to [0, 1). */
  result.tx_ts_int = nr_toa_get_last_tx_timestamp();
  result.fs_hz = blk->fs_hz;
  if (result.tx_ts_int > 0 && rx_int >= result.tx_ts_int) {
    const uint64_t difference = rx_int - result.tx_ts_int;
    if (difference > INT64_MAX) return -1;
    result.toa_samp_int = (int64_t)difference;
    result.toa_samp_frac = result.rx_ts_frac;
    result.toa_ns = ((double)difference + result.rx_ts_frac) * 1.0e9 / result.fs_hz;
    result.range_m = result.toa_ns * 1.0e-9 * 299792458.0;
    result.valid = 1;
  }
  result.pci = sync->pci;
  result.ssb_index = sync->ssb_index;
  result.sfn = sync->sfn;
  result.slot = sync->slot;
  result.snr_db = sync->snr_db;
  /* Integer tracking already moved the FFT window. The CIR supplies its
   * remaining delay; frac_offset_samp is diagnostic, not a second correction. */
  *meas = result;
  return 0;
}

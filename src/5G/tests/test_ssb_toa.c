#include "nr_pos_api.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "SSB TOA check failed at line %d: %s\n", __LINE__, #c); return 1; } } while (0)

/* Independent physical single-path model, not the project's TX generator.
 * A shifted FFT window changes residual delay but not arrival time. */
static int check_single_path(uint32_t scs, double fs, double delay_bins, int window_shift)
{
  cf32_t frequency[NR_SSB_RE_ROWS * NR_SSB_RE_COLS];
  nr_chest_full_t hf = {.h_full = frequency, .n_re = NR_SSB_RE_ROWS * NR_SSB_RE_COLS};
  nr_cir_t cir;
  nr_sync_state_t sync;
  nr_iq_block_t block;
  nr_toa_meas_t meas;
  int peak;
  double frac, samples;
  const double step_s = 1.0 / (240.0 * scs * 1000.0);
  const double true_delay_s = delay_bins * step_s;
  const double residual_s = true_delay_s - window_shift / fs;
  nr_v0_set_ssb_scs_khz(scs);
  for (unsigned sym = 0; sym < NR_SSB_RE_ROWS; ++sym) {
    for (unsigned rel = 0; rel < NR_SSB_RE_COLS; ++rel) {
      const double phase = -2 * M_PI * ((int)rel - 120) * scs * 1000.0 * residual_s;
      frequency[sym * NR_SSB_RE_COLS + rel] = (cf32_t){(float)cos(phase), (float)sin(phase)};
    }
  }
  CHECK(nr_ssb_build_cir(&hf, &cir) == 0);
  CHECK(fabs(cir.bin_width_s - step_s) < 1e-20);
  CHECK(nr_toa_find_integer_peak(&cir, &peak) == 0);
  CHECK(nr_toa_refine_fractional(&cir, peak, &frac) == 0);
  CHECK(nr_toa_cir_delay_samples(&cir, peak, frac, fs, &samples) == 0);
  /* A three-point amplitude parabola is approximate for a sampled sinc.
   * Bound its error to a quarter CIR bin; do not claim exact sub-bin timing. */
  CHECK(fabs(samples / fs - residual_s) < 0.25 * step_s + 1e-14);
  if (window_shift == 0 && delay_bins == floor(delay_bins))
    CHECK(fabs(samples / fs - true_delay_s) < 1e-12 * step_s);
  memset(&sync, 0, sizeof(sync));
  memset(&block, 0, sizeof(block));
  block.ts_first = UINT64_C(10000000000000001);
  block.fs_hz = fs;
  sync.coarse_offset_samp = 100;
  sync.cum_tracking_shift_samp = window_shift;
  sync.frac_offset_samp = 0.49f; /* Must not double-count a window diagnostic. */
  CHECK(nr_toa_build_meas(&sync, &block, samples, &meas) == 0);
  const double arrival = (double)((int64_t)meas.rx_ts_int - (int64_t)block.ts_first) + meas.rx_ts_frac;
  CHECK(fabs(arrival / fs - (100.0 / fs + true_delay_s)) < 0.25 * step_s + 1e-14);
  CHECK(meas.rx_ts_frac >= 0 && meas.rx_ts_frac < 1);
  CHECK(!meas.valid); /* No TX event: relative timing must not imply absolute range. */
  return 0;
}

static int check_boundaries(void)
{
  cf32_t bins[8] = {{0}};
  nr_cir_t cir = {.cir = bins, .cir_len = 8, .bin_width_s = 0.125};
  double frac, samples;
  int peak;
  /* Samples of 10 - (x + 0.25)^2 around circular index zero. */
  bins[7].r = 9.4375f; bins[0].r = 9.9375f; bins[1].r = 8.4375f;
  CHECK(nr_toa_find_integer_peak(&cir, &peak) == 0 && peak == 0);
  CHECK(nr_toa_refine_fractional(&cir, peak, &frac) == 0 && fabs(frac + 0.25) < 1e-12);
  CHECK(nr_toa_cir_delay_samples(&cir, peak, frac, 8, &samples) == 0 && fabs(samples + 0.25) < 1e-12);
  memset(bins, 0, sizeof(bins));
  bins[6].r = 8.4375f; bins[7].r = 9.9375f; bins[0].r = 9.4375f;
  CHECK(nr_toa_find_integer_peak(&cir, &peak) == 0 && peak == 7);
  CHECK(nr_toa_refine_fractional(&cir, peak, &frac) == 0 && fabs(frac - 0.25) < 1e-12);
  CHECK(nr_toa_cir_delay_samples(&cir, peak, frac, 8, &samples) == 0 && fabs(samples + 0.75) < 1e-12);
  CHECK(nr_toa_cir_delay_samples(&cir, 4, 0, 8, &samples) == 0 && samples == -4);
  cir.cir_len = 7;
  CHECK(nr_toa_cir_delay_samples(&cir, 3, 0, 8, &samples) == 0 && samples == 3);
  CHECK(nr_toa_cir_delay_samples(&cir, 4, 0, 8, &samples) == 0 && samples == -3);
  cir.bin_width_s = 0;
  CHECK(nr_toa_cir_delay_samples(&cir, 0, 0, 8, &samples) != 0);
  cir.bin_width_s = NAN;
  CHECK(nr_toa_cir_delay_samples(&cir, 0, 0, 8, &samples) != 0);
  cir.bin_width_s = 0.125;
  CHECK(nr_toa_cir_delay_samples(&cir, -1, 0, 8, &samples) != 0);
  CHECK(nr_toa_cir_delay_samples(&cir, 7, 0, 8, &samples) != 0);
  CHECK(nr_toa_cir_delay_samples(&cir, 0, NAN, 8, &samples) != 0);
  CHECK(nr_toa_cir_delay_samples(&cir, 0, .6, 8, &samples) != 0);
  CHECK(nr_toa_cir_delay_samples(&cir, 0, 0, INFINITY, &samples) != 0);
  CHECK(nr_toa_refine_fractional(&cir, -1, &frac) != 0);
  CHECK(nr_toa_refine_fractional(&cir, 7, &frac) != 0);
  memset(bins, 0, sizeof(bins));
  CHECK(nr_toa_find_integer_peak(&cir, &peak) != 0);
  bins[0].r = NAN;
  CHECK(nr_toa_find_integer_peak(&cir, &peak) != 0);
  cir.cir = NULL;
  CHECK(nr_toa_find_integer_peak(&cir, &peak) != 0);
  CHECK(nr_toa_refine_fractional(&cir, 0, &frac) != 0);
  return 0;
}

static int check_provider(void)
{
  c16_t iq[8192] = {{0}};
  nr_iq_block_t block = {0};
  const int insertion = 256;
  nr_v0_set_ssb_scs_khz(30);
  nr_v0_set_ssb_abs_start_symbol(-1);
  CHECK(nr_v0_ssb_build_burst_iq(0, 1, iq + insertion, nr_v0_ssb_burst_len(), 3500) > 0);
  block.rx[0] = iq; block.nsamps = 8192; block.fs_hz = 30720000;
  block.ts_first = UINT64_C(10000000000000001);
  for (int window = -3; window <= 3; window += 3) {
    nr_sync_state_t sync = {0};
    nr_toa_meas_t meas;
    sync.pci = 1; sync.ssb_index = NR_V0_SYNTH_PBCH_SSB_IDX;
    sync.locked = sync.mib_ok = 1;
    sync.coarse_offset_samp = insertion;
    sync.cum_tracking_shift_samp = window;
    CHECK(nr_ssb_provider.extract_meas(NULL, &block, &sync, &meas) == 0);
    const double measured = (double)((int64_t)meas.rx_ts_int - (int64_t)block.ts_first) + meas.rx_ts_frac;
    CHECK(fabs(measured - insertion) < 1.5); /* Existing 240-bin interpolation limit. */
    CHECK(!meas.valid);
  }
  memset(iq, 0, sizeof(iq));
  nr_sync_state_t sync = {0}; sync.pci = 1; sync.locked = sync.mib_ok = 1;
  nr_toa_meas_t meas = {0};
  CHECK(nr_ssb_provider.extract_meas(NULL, &block, &sync, &meas) != 0);
  return 0;
}

static int accept_write(openair0_device_t *dev, openair0_timestamp_t *ts, void **buffer,
                        uint32_t n, int antenna, int flags)
{
  (void)dev; (void)ts; (void)buffer; (void)antenna; (void)flags;
  return (int)n;
}

static int check_timestamps(void)
{
  nr_sync_state_t sync = {0};
  nr_iq_block_t block = {0};
  nr_toa_meas_t meas;
  block.fs_hz = 30720000;
  block.ts_first = UINT64_C(10000000000000001);
  CHECK(nr_toa_build_meas(&sync, &block, -1.25, &meas) == 0);
  CHECK(meas.rx_ts_int == block.ts_first - 2 && meas.rx_ts_frac == .75);
  CHECK(nr_toa_build_meas(&sync, &block, 1.25, &meas) == 0);
  CHECK(meas.rx_ts_int == block.ts_first + 1 && meas.rx_ts_frac == .25);
  block.ts_first = 0;
  CHECK(nr_toa_build_meas(&sync, &block, -.25, &meas) != 0 && meas.fs_hz == 0);
  block.ts_first = UINT64_MAX;
  CHECK(nr_toa_build_meas(&sync, &block, 1, &meas) != 0);
  sync.cum_tracking_shift_samp = INT64_MAX; sync.coarse_offset_samp = 1;
  CHECK(nr_toa_build_meas(&sync, &block, 0, &meas) != 0);
  sync.cum_tracking_shift_samp = 0; sync.coarse_offset_samp = 0;
  CHECK(nr_toa_build_meas(&sync, &block, NAN, &meas) != 0);
  CHECK(nr_toa_build_meas(&sync, &block, 0x1p63, &meas) != 0);
  block.fs_hz = 0;
  CHECK(nr_toa_build_meas(&sync, &block, 0, &meas) != 0);
  block.fs_hz = 30720000;
  /* A test-only TX callback checks the valid branch without an SDR. */
  openair0_device_t device = {0}; device.trx_write_func = accept_write;
  nr_ssb_tx_plan_t plan = {0}; plan.tx_hw_timestamp = UINT64_C(10000000000000001);
  nr_tx_burst_t burst = {0}; burst.nsamps = 1;
  CHECK(nr_anchor_write_burst(&device, &plan, &burst) == 1);
  block.ts_first = plan.tx_hw_timestamp;
  CHECK(nr_toa_build_meas(&sync, &block, 10.25, &meas) == 0 && meas.valid);
  CHECK(meas.toa_samp_int == 10 && meas.toa_samp_frac == .25);
  CHECK(fabs(meas.toa_ns - 10.25e9 / block.fs_hz) < 1e-9);
  CHECK(nr_toa_build_meas(&sync, &block, -.25, &meas) == 0 && !meas.valid);
  return 0;
}

int main(void)
{
  const double rates[] = {7680000, 15360000, 30720000, 61440000};
  const double bins[] = {-7, -3, -1, 0, 1, 3, 7, -.25, .25, 2.5, -2.5};
  for (unsigned scs = 15; scs <= 30; scs += 15)
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r)
      for (unsigned d = 0; d < sizeof(bins) / sizeof(bins[0]); ++d)
        for (int shift = -1; shift <= 1; ++shift)
          CHECK(check_single_path(scs, rates[r], bins[d], shift) == 0);
  CHECK(check_boundaries() == 0);
  CHECK(check_provider() == 0);
  CHECK(check_timestamps() == 0);
  nr_v0_set_ssb_scs_khz(30);
  puts("SSB TOA: signed CIR timing, units, window invariance and split timestamps passed");
  return 0;
}

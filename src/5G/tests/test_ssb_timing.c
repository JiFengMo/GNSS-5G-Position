#include "nr_pos_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "SSB timing check failed at %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
  c16_t iq[8192] = {{0}};
  nr_iq_block_t block = {0};
  const int insertion = 256;
  int interior = 0, moved = 0;
  nr_v0_set_ssb_scs_khz(30);
  nr_v0_set_ssb_abs_start_symbol(-1);
  CHECK(nr_v0_ssb_build_burst_iq(0, 1, iq + insertion,
                               nr_v0_ssb_burst_len(), 3500) > 0);
  block.rx[0] = iq; block.nsamps = 8192; block.fs_hz = 30720000;

  /* Measure the actual frontend at explicit physical offsets, independent
   * of the production candidate ordering and interpolation implementation. */
  for (int initial = -2; initial <= 2; ++initial) {
    float metrics[5] = {0};
    nr_sync_state_t sync = {0};
    int best_shift = 0, actual_shift;
    double expected_fraction = .2 * .85;
    sync.pci = 1; sync.ssb_index = NR_V0_SYNTH_PBCH_SSB_IDX;
    sync.coarse_offset_samp = insertion + initial;
    sync.frac_offset_samp = .2f;
    for (int offset = -2; offset <= 2; ++offset) {
      nr_sync_state_t trial = sync;
      nr_ssb_window_t win;
      nr_ssb_grid_t grid;
      trial.cum_tracking_shift_samp = offset;
      CHECK(nr_ssb_extract_window(&block, &trial, &win) == 0);
      CHECK(nr_ssb_demod(&block, &win, 0, &grid) == 0);
      CHECK(nr_ssb_pbch_prepare_frontend(&grid, 1, sync.ssb_index, 0,
                                        &metrics[offset + 2], NULL, NULL, NULL) == 0);
    }
    for (int offset = -2; offset <= 2; ++offset) {
      if (metrics[offset + 2] > metrics[best_shift + 2] ||
          (metrics[offset + 2] == metrics[best_shift + 2] && abs(offset) < abs(best_shift)))
        best_shift = offset;
    }
    if (abs(best_shift) < 2) {
      const double left = metrics[best_shift + 1], center = metrics[best_shift + 2];
      const double right = metrics[best_shift + 3];
      const double curvature = left - 2 * center + right;
      if (curvature < -1e-6) {
        const double fraction = fmax(-.5, fmin(.5, (left - right) / (2 * curvature)));
        expected_fraction = .7 * .2 + .3 * fraction;
        ++interior;
      }
    }
    CHECK(nr_ssb_track_timing(&block, &sync, &actual_shift) == 0);
    CHECK(actual_shift == best_shift && sync.cum_tracking_shift_samp == best_shift);
    CHECK(fabs(sync.frac_offset_samp - expected_fraction) < 1e-6);
    CHECK(fabs(sync.frac_offset_samp) <= .5);
    moved += actual_shift != 0;
  }
  CHECK(interior > 0 && moved > 0);
  memset(iq, 0, sizeof(iq));
  nr_sync_state_t sync = {0};
  sync.pci = 1; sync.coarse_offset_samp = 0; sync.frac_offset_samp = .2f;
  int shift = 99;
  CHECK(nr_ssb_track_timing(&block, &sync, &shift) == 0);
  CHECK(shift == 0 && sync.cum_tracking_shift_samp == 0);
  CHECK(fabs(sync.frac_offset_samp - .17) < 1e-6);
  puts("SSB timing: ordered physical neighbors, bounded fractional fit and missing candidates passed");
  return 0;
}

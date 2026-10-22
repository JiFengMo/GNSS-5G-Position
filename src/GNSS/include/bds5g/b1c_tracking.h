#pragma once
#include "bds5g/b1c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BDS_B1C_PULL_IN = 0,
  BDS_B1C_LOCKED = 1,
  BDS_B1C_LOST = 2
} bds_b1c_lock_state_t;

typedef struct {
  double sample_rate_hz;
  double if_hz;
  double carrier_rf_hz;
  double dll_bandwidth_hz;
  double dll_damping;
  double early_late_offset_chips; /* Half E-L spacing, primary-code chips. */
  double pll_bandwidth_hz;        /* CU-SDR third-order PLL, <=18 Hz. */
  unsigned quality_epochs;       /* Nonoverlapping full-code windows. */
  double min_cn0_db_hz;
  double min_lock_indicator;
  double max_residual_frequency_hz; /* Half-epoch phase difference guard. */
  unsigned good_windows_to_lock;
  unsigned bad_windows_to_lose;
} bds_b1c_tracking_options_t;

typedef struct { double i, q; } bds_b1c_correlation_t;

typedef struct {
  int prn;
  uint64_t epoch;
  uint64_t generation;           /* Increments on explicit reacquisition reset. */
  uint64_t absolute_sample;      /* Record-relative first sample, zero-based. */
  size_t sample_count;
  double sample_time_s;          /* absolute_sample/fs, NOT BDT/UTC. */
  double integration_s;
  double carrier_hz;             /* NCO used for this epoch, before update. */
  double doppler_hz;
  double code_rate_hz;
  double rem_code_phase;         /* Primary-code chips at epoch start. */
  double rem_carrier_phase;      /* Carrier NCO radians at epoch start. */
  double carrier_cycles;         /* Unwrapped NCO cycles since generation start. */
  double dll_error;
  double pll_error;              /* Costas cycles, [-.25,.25]. */
  double residual_frequency_hz;  /* Half-epoch prompt FLL diagnostic. */
  double code_nco_hz;
  double carrier_nco_hz;
  bds_b1c_correlation_t data_early, data_prompt, data_late;
  bds_b1c_correlation_t pilot_early, pilot_prompt, pilot_late;
  double data_cn0_db_hz, pilot_cn0_db_hz, cn0_db_hz;
  double data_lock_indicator, pilot_lock_indicator, lock_indicator;
  uint8_t quality_updated;
  uint8_t quality_good;
  bds_b1c_lock_state_t state;
  uint8_t valid;                 /* Tracking quality only, not navigation validity. */
} bds_b1c_tracking_epoch_t;

typedef struct bds_b1c_tracker bds_b1c_tracker_t;
typedef void (*bds_b1c_tracking_callback_t)(const bds_b1c_tracking_epoch_t *, void *);

void bds_b1c_tracking_defaults(bds_b1c_tracking_options_t *options);
int bds_b1c_tracker_create(const bds_b1c_tracking_options_t *options,
                            const bds_b1c_acquisition_t *seed, uint64_t window_start,
                            bds_b1c_tracker_t **tracker);
void bds_b1c_tracker_destroy(bds_b1c_tracker_t *tracker);
int bds_b1c_tracker_reset(bds_b1c_tracker_t *tracker,
                          const bds_b1c_acquisition_t *seed, uint64_t window_start);
/* Arbitrary chunk sizes, no retained IQ. Input must start at next_sample().
 * Each full 10230-chip epoch invokes callback synchronously; do not reenter.
 * -1 invalid/nonfinite input (no samples consumed), -2 internal loop bounds,
 * -3 discontinuity (tracker becomes LOST; explicit reset required).
 * Incomplete final epochs are retained, never emitted as full epochs. */
int bds_b1c_tracker_push(bds_b1c_tracker_t *tracker, const bds5g_cf32_t *samples,
                         size_t count, uint64_t first_sample,
                         bds_b1c_tracking_callback_t callback, void *context);
uint64_t bds_b1c_tracker_next_sample(const bds_b1c_tracker_t *tracker);
uint64_t bds_b1c_tracker_epoch_count(const bds_b1c_tracker_t *tracker);
size_t bds_b1c_tracker_pending_samples(const bds_b1c_tracker_t *tracker);
size_t bds_b1c_tracker_samples_to_epoch(const bds_b1c_tracker_t *tracker);
bds_b1c_lock_state_t bds_b1c_tracker_state(const bds_b1c_tracker_t *tracker);

#ifdef __cplusplus
}
#endif

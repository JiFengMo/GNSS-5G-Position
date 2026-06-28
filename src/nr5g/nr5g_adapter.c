#include "bds5g/nr5g.h"

#include <math.h>
#include <string.h>

int bds5g_nr_to_observation(const bds5g_nr_anchor_t *anchor,
                            const bds5g_nr_toa_t *measurement,
                            bds5g_observation_t *observation)
{
  double corrected_toa_ns;
  if (!anchor || !measurement || !observation || !measurement->valid ||
      !anchor->absolute_time_valid || measurement->pci != anchor->pci) {
    return -1;
  }

  corrected_toa_ns = measurement->toa_ns - anchor->calibrated_delay_ns;
  if (!isfinite(corrected_toa_ns) || corrected_toa_ns <= 0.0) {
    return -2;
  }

  memset(observation, 0, sizeof(*observation));
  observation->timestamp_ns = measurement->timestamp_ns;
  observation->source = BDS5G_SOURCE_NR_SSB;
  observation->source_id = anchor->anchor_id;
  observation->source_ecef_m = anchor->ecef_m;
  observation->pseudorange_m = corrected_toa_ns * 1.0e-9 *
                               BDS5G_SPEED_OF_LIGHT_MPS;
  observation->sigma_m = 12.0 / fmax(measurement->peak_metric, 1.0);
  if (observation->sigma_m < 0.5) {
    observation->sigma_m = 0.5;
  }
  observation->cn0_db_hz = measurement->snr_db;
  observation->valid = 1U;
  return 0;
}


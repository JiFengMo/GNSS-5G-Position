#include "bds5g/nr5g.h"
#include "bds5g/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
  fprintf(stderr,
          "Usage: %s --output FILE --timestamp-ns NS --anchor-id N --pci N "
          "--x M --y M --z M --toa-ns NS --delay-ns NS "
          "[--snr-db DB] [--peak-metric VALUE] [--append]\n",
          program);
}

int main(int argc, char **argv)
{
  bds5g_nr_anchor_t anchor;
  bds5g_nr_toa_t toa;
  bds5g_observation_t observation;
  const char *output_path = NULL;
  int append = 0;

  memset(&anchor, 0, sizeof(anchor));
  memset(&toa, 0, sizeof(toa));
  anchor.absolute_time_valid = 1U;
  toa.peak_metric = 1.0;
  toa.valid = 1U;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
      output_path = argv[++i];
    } else if (strcmp(argv[i], "--timestamp-ns") == 0 && i + 1 < argc) {
      toa.timestamp_ns = (uint64_t)strtoull(argv[++i], NULL, 10);
    } else if (strcmp(argv[i], "--anchor-id") == 0 && i + 1 < argc) {
      anchor.anchor_id = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--pci") == 0 && i + 1 < argc) {
      anchor.pci = toa.pci = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--x") == 0 && i + 1 < argc) {
      anchor.ecef_m.x = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--y") == 0 && i + 1 < argc) {
      anchor.ecef_m.y = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--z") == 0 && i + 1 < argc) {
      anchor.ecef_m.z = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--toa-ns") == 0 && i + 1 < argc) {
      toa.toa_ns = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--delay-ns") == 0 && i + 1 < argc) {
      anchor.calibrated_delay_ns = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--snr-db") == 0 && i + 1 < argc) {
      toa.snr_db = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--peak-metric") == 0 && i + 1 < argc) {
      toa.peak_metric = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--append") == 0) {
      append = 1;
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (!output_path || toa.timestamp_ns == 0U || toa.toa_ns <= 0.0) {
    usage(argv[0]);
    return 2;
  }
  if (bds5g_nr_to_observation(&anchor, &toa, &observation) != 0) {
    fprintf(stderr, "failed to convert NR TOA measurement\n");
    return 3;
  }
  if (bds5g_append_observation_csv(output_path, &observation, append ? 0 : 1) != 0) {
    fprintf(stderr, "failed to write observation: %s\n", output_path);
    return 4;
  }
  return 0;
}

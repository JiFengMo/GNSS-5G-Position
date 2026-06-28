#include "bds5g/b1c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
  fprintf(stderr,
          "Usage: %s --input IQ_CF32 --prn N --sample-rate HZ "
          "[--doppler-min HZ] [--doppler-max HZ] [--doppler-step HZ]\n",
          program);
}

int main(int argc, char **argv)
{
  const char *input_path = NULL;
  int prn = 19;
  double sample_rate_hz = 0.0;
  double doppler_min_hz = -8000.0;
  double doppler_max_hz = 8000.0;
  double doppler_step_hz = 250.0;
  FILE *stream;
  long file_bytes;
  size_t sample_count;
  bds5g_cf32_t *samples;
  bds_b1c_acquisition_t acquisition;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      input_path = argv[++i];
    } else if (strcmp(argv[i], "--prn") == 0 && i + 1 < argc) {
      prn = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--sample-rate") == 0 && i + 1 < argc) {
      sample_rate_hz = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--doppler-min") == 0 && i + 1 < argc) {
      doppler_min_hz = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--doppler-max") == 0 && i + 1 < argc) {
      doppler_max_hz = strtod(argv[++i], NULL);
    } else if (strcmp(argv[i], "--doppler-step") == 0 && i + 1 < argc) {
      doppler_step_hz = strtod(argv[++i], NULL);
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (!input_path || sample_rate_hz <= 0.0) {
    usage(argv[0]);
    return 2;
  }
  stream = fopen(input_path, "rb");
  if (!stream || fseek(stream, 0L, SEEK_END) != 0) {
    fprintf(stderr, "failed to open IQ input: %s\n", input_path);
    if (stream) fclose(stream);
    return 3;
  }
  file_bytes = ftell(stream);
  rewind(stream);
  if (file_bytes <= 0 || file_bytes % (long)sizeof(bds5g_cf32_t) != 0) {
    fclose(stream);
    fprintf(stderr, "input must contain interleaved float32 IQ samples\n");
    return 4;
  }
  sample_count = (size_t)file_bytes / sizeof(bds5g_cf32_t);
  samples = (bds5g_cf32_t *)malloc(sample_count * sizeof(*samples));
  if (!samples || fread(samples, sizeof(*samples), sample_count, stream) != sample_count) {
    free(samples);
    fclose(stream);
    fprintf(stderr, "failed to read IQ input\n");
    return 5;
  }
  fclose(stream);
  if (bds_b1c_acquire(samples,
                      sample_count,
                      sample_rate_hz,
                      prn,
                      doppler_min_hz,
                      doppler_max_hz,
                      doppler_step_hz,
                      &acquisition) != 0) {
    free(samples);
    fprintf(stderr, "B1C acquisition failed\n");
    return 6;
  }
  free(samples);
  printf("prn,doppler_hz,code_phase_samples,peak_metric,cn0_db_hz,acquired\n");
  printf("%d,%.3f,%zu,%.6f,%.3f,%u\n",
         acquisition.prn,
         acquisition.doppler_hz,
         acquisition.code_phase_samples,
         acquisition.peak_metric,
         acquisition.cn0_db_hz,
         (unsigned)acquisition.acquired);
  return acquisition.acquired ? 0 : 7;
}

#include "bds5g/b1c.h"
#include "bds5g/b1c_parallel.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

_Static_assert(sizeof(float) == 4, "cf32 input requires 32-bit floats");

static void usage(const char *program)
{
  fprintf(stderr,
    "Usage: %s --input FILE --sample-rate HZ [options]\n"
    "  --threads N             PRN workers, 1..63; default 1 (serial)\n"
    "  --input-format ci8|cf32    Interleaved I,Q (cf32: little-endian), default cf32\n"
    "  --prn N | --prns LIST      Comma-separated PRNs/ranges (default 1-63)\n"
    "  --if HZ                   Residual IF, default 0; carrier = IF + Doppler\n"
    "  --doppler-min HZ          Default -5000\n"
    "  --doppler-max HZ          Default 5000\n"
    "  --doppler-step HZ         Default 50\n"
    "  --fine-step HZ            Default 25 (must divide coarse step)\n"
    "  --coherent-ms MS          Default 10, range 1..10\n"
    "  --threshold VALUE         Default 10 (CU-SDR statistic, not C/N0)\n"
    "  --skip-samples N          First complex sample index, default 0\n"
    "  --output CSV              Default stdout; read only a 20 ms window\n"
    "No tracking/navigation or device control is performed by this offline tool.\n",
    program);
}

static int number(const char *text, double *out)
{
  char *end; errno = 0;
  *out = strtod(text, &end);
  return *text && !*end && !errno && isfinite(*out) ? 0 : -1;
}

static int prn_list(const char *text, int *prns, size_t *count)
{
  const char *p = text;
  *count = 0;
  while (*p) {
    char *end; errno = 0;
    if (*p < '0' || *p > '9') return -1;
    long first = strtol(p, &end, 10), last = first;
    if (errno || first < 1 || first > 63) return -1;
    p = end;
    if (*p == '-') {
      ++p;
      if (*p < '0' || *p > '9') return -1;
      last = strtol(p, &end, 10); p = end;
      if (errno || last < first || last > 63) return -1;
    }
    for (long n = first; n <= last; ++n) {
      if (*count >= 63) return -1;
      for (size_t k = 0; k < *count; ++k) if (prns[k] == n) return -1;
      prns[(*count)++] = (int)n;
    }
    if (!*p) return 0;
    if (*p++ != ',' || !*p) return -1;
  }
  return -1;
}

static int read_iq(const char *path, int ci8, uint64_t skip, size_t count,
                    bds5g_cf32_t *samples)
{
  const size_t width = ci8 ? 2U : 8U;
  if (skip > (uint64_t)INT64_MAX / width) return -1;
  FILE *file = fopen(path, "rb");
  if (!file) return -1;
#ifdef _WIN32
  int seek_error = _fseeki64(file, (__int64)(skip * width), SEEK_SET);
#else
  int seek_error = fseeko(file, (off_t)(skip * width), SEEK_SET);
#endif
  if (seek_error) { fclose(file); return -1; }
  unsigned char *raw = malloc(count * width);
  if (!raw || fread(raw, width, count, file) != count) {
    free(raw); fclose(file); return -1;
  }
  fclose(file);
  for (size_t k = 0; k < count; ++k) {
    if (ci8) {
      samples[k].i = raw[2*k] < 128 ? raw[2*k] : (int)raw[2*k] - 256;
      samples[k].q = raw[2*k+1] < 128 ? raw[2*k+1] : (int)raw[2*k+1] - 256;
    } else {
      for (size_t j = 0; j < 2; ++j) {
        const unsigned char *b = raw + 8*k + 4*j;
        uint32_t word = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                        ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        float value; memcpy(&value, &word, sizeof(value));
        if (j == 0) samples[k].i = value; else samples[k].q = value;
      }
    }
  }
  free(raw); return 0;
}

int main(int argc, char **argv)
{
  const char *input = NULL, *output = NULL, *list = "1-63";
  int ci8 = 0; unsigned threads = 1;
  uint64_t skip = 0;
  bds_b1c_acquisition_options_t o;
  bds_b1c_acquisition_defaults(&o); o.sample_rate_hz = 0;
  for (int i = 1; i < argc; ++i) {
    const char *key = argv[i];
    if (!strcmp(key, "--help")) { usage(argv[0]); return 0; }
    if (i + 1 >= argc) goto invalid;
    const char *value = argv[++i];
    double *numeric = NULL;
    if (!strcmp(key, "--input")) input = value;
    else if (!strcmp(key, "--output")) output = value;
    else if (!strcmp(key, "--prn") || !strcmp(key, "--prns")) list = value;
    else if (!strcmp(key, "--threads")) {
      double n;
      if (number(value,&n) || n<1 || n>63 || n!=floor(n)) goto invalid;
      threads=(unsigned)n;
    } else if (!strcmp(key, "--input-format")) {
      if (strcmp(value, "ci8") && strcmp(value, "cf32")) goto invalid;
      ci8 = !strcmp(value, "ci8");
    } else if (!strcmp(key, "--skip-samples")) {
      char *end; errno = 0;
      if (*value < '0' || *value > '9') goto invalid;
      skip = strtoull(value, &end, 10);
      if (*end || errno) goto invalid;
    } else if (!strcmp(key, "--sample-rate")) numeric = &o.sample_rate_hz;
    else if (!strcmp(key, "--if")) numeric = &o.if_hz;
    else if (!strcmp(key, "--doppler-min")) numeric = &o.doppler_min_hz;
    else if (!strcmp(key, "--doppler-max")) numeric = &o.doppler_max_hz;
    else if (!strcmp(key, "--doppler-step")) numeric = &o.doppler_step_hz;
    else if (!strcmp(key, "--fine-step")) numeric = &o.fine_step_hz;
    else if (!strcmp(key, "--coherent-ms")) numeric = &o.coherent_integration_ms;
    else if (!strcmp(key, "--threshold")) numeric = &o.threshold;
    else goto invalid;
    if (numeric && number(value, numeric)) goto invalid;
  }
  int prns[63]; size_t count;
  if (!input || o.sample_rate_hz < 2.0 * BDS_B1C_CHIP_RATE_HZ ||
      o.sample_rate_hz > 1e8 || prn_list(list, prns, &count)) goto invalid;
  if (output) {
    struct stat input_stat, output_stat;
    if (!strcmp(input, output) ||
        (!stat(input, &input_stat) && !stat(output, &output_stat) &&
         input_stat.st_dev == output_stat.st_dev && input_stat.st_ino == output_stat.st_ino)) {
      fprintf(stderr, "Output must not overwrite the IQ input\n"); return 2;
    }
  }
  const size_t samples_count = 2U * (size_t)llround(o.sample_rate_hz * .01);
  bds5g_cf32_t *samples = malloc(samples_count * sizeof(*samples));
  bds_b1c_acquisition_t results[63];
  if (!samples) { fprintf(stderr, "Cannot allocate IQ window\n"); return 3; }
  if (read_iq(input, ci8, skip, samples_count, samples)) {
    fprintf(stderr, "Cannot read a complete 20 ms IQ window at sample %llu: %s\n",
            (unsigned long long)skip, input); free(samples); return 3;
  }
  fprintf(stderr, "B1C offline acquisition: %zu PRNs, %zu samples, fs=%.0f, IF=%.3f\n",
          count, samples_count, o.sample_rate_hz, o.if_hz);
  const int error = bds_b1c_acquire_many_parallel(samples, samples_count, &o, prns, count, results, threads);
  free(samples);
  if (error) { fprintf(stderr, "Acquisition error %d: check finite IQ, search grids and options\n", error); return 6; }
  FILE *csv = output ? fopen(output, "w") : stdout;
  if (!csv) { fprintf(stderr, "Cannot open output: %s\n", output); return 3; }
  fprintf(csv, "prn,carrier_hz,doppler_hz,code_phase_samples,peak_metric,cn0_db_hz,acquired,coarse_doppler_hz,fine_metric,input_start_sample\n");
  int acquired = 0;
  for (size_t k = 0; k < count; ++k) {
    bds_b1c_acquisition_t *r = &results[k];
    fprintf(csv, "%d,%.12g,%.12g,%zu,%.17g,nan,%u,%.12g,%.17g,%llu\n",
      r->prn, r->carrier_hz, r->doppler_hz, r->code_phase_samples,
      r->peak_metric, (unsigned)r->acquired, r->coarse_doppler_hz, r->fine_metric,
      (unsigned long long)skip);
    acquired += r->acquired != 0;
  }
  int io_error = ferror(csv);
  if (output) io_error |= fclose(csv) != 0;
  else io_error |= fflush(csv) != 0;
  fprintf(stderr, "Acquired %d/%zu PRNs. Code phase is relative to the IQ window, not propagation time.\n", acquired, count);
  return io_error ? 3 : acquired ? 0 : 7;
invalid:
  usage(argv[0]); return 2;
}

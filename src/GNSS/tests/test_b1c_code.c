#include "bds5g/b1c.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash(const int8_t *data, size_t count)
{
  uint64_t h = UINT64_C(14695981039346656037);
  for (size_t k = 0; k < count; ++k) h = (h ^ (uint8_t)data[k]) * UINT64_C(1099511628211);
  return h;
}

static void binary_check(FILE *f, const int8_t *data, size_t count)
{
  for (size_t k = 0; k < count; ++k) {
    int actual = fgetc(f);
    if (actual != (uint8_t)data[k]) {
      fprintf(stderr, "MATLAB binary mismatch at vector offset %zu\n", k);
      abort();
    }
  }
}

static FILE *open_in(const char *directory, const char *name)
{
  char path[4096];
  assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
  FILE *f = fopen(path, "rb"); assert(f); return f;
}

int main(int argc, char **argv)
{
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s CODE_HASHES_CSV SAMPLED_HASHES_CSV [MATLAB_BASELINE_DIR]\n", argv[0]);
    return 2;
  }
  FILE *csv = fopen(argv[1], "r"); assert(csv);
  FILE *refs[3] = {NULL, NULL, NULL};
  if (argc == 4) {
    refs[0] = open_in(argv[3], "data_primary.i8");
    refs[1] = open_in(argv[3], "pilot_primary.i8");
    refs[2] = open_in(argv[3], "pilot_secondary.i8");
  }
  char line[512]; assert(fgets(line, sizeof(line), csv));
  int8_t chips[BDS_B1C_PRIMARY_CODE_LENGTH];
  for (int prn = 1; prn <= 63; ++prn) {
    int row; unsigned long long expected[3];
    assert(fgets(line, sizeof(line), csv));
    assert(sscanf(line, "%d,%llx,%llx,%llx", &row, &expected[0], &expected[1], &expected[2]) == 4);
    assert(row == prn);
    for (int component = 0; component < 3; ++component) {
      size_t length = component == 2 ? BDS_B1C_SECONDARY_CODE_LENGTH : BDS_B1C_PRIMARY_CODE_LENGTH;
      int error = component == 2 ? bds_b1c_generate_secondary_code(prn, chips, sizeof(chips)) :
        bds_b1c_generate_code(prn, (bds_b1c_component_t)component, chips, sizeof(chips));
      assert(!error);
      assert(hash(chips, length) == (uint64_t)expected[component]);
      if (refs[component]) binary_check(refs[component], chips, length);
    }
  }
  assert(!fgets(line, sizeof(line), csv)); fclose(csv);
  for (int k = 0; k < 3; ++k) if (refs[k]) { assert(fgetc(refs[k]) == EOF); fclose(refs[k]); }

  /* Independent published ICD Table 5-4 first/last 24 bits of PRN 1. */
  assert(!bds_b1c_generate_secondary_code(1, chips, sizeof(chips)));
  unsigned first = 0, last = 0;
  for (size_t k = 0; k < 24; ++k) {
    first = (first << 1) | (chips[k] < 0);
    last = (last << 1) | (chips[1800 - 24 + k] < 0);
  }
  assert(first == 027516364U && last == 067377026U);

  csv = fopen(argv[2], "r"); assert(csv); assert(fgets(line, sizeof(line), csv));
  int8_t *sampled = malloc(180000); assert(sampled);
  FILE *sample_ref = NULL; int previous_fs = 0, rows = 0;
  while (fgets(line, sizeof(line), csv)) {
    int fs, prn, component; unsigned long long expected;
    assert(sscanf(line, "%d,%d,%d,%llx", &fs, &prn, &component, &expected) == 4);
    assert(fs == 4092000 || fs == 18000000);
    size_t length = (size_t)llround(fs * .01);
    assert(!bds_b1c_sample_boc11(prn, (bds_b1c_component_t)component, fs, sampled, length));
    if (hash(sampled, length) != (uint64_t)expected) {
      fprintf(stderr, "Sampled waveform differs: fs=%d PRN=%d component=%d\n", fs, prn, component);
      abort();
    }
    if (argc == 4) {
      if (fs != previous_fs) {
        if (sample_ref) { assert(fgetc(sample_ref) == EOF); fclose(sample_ref); }
        char name[64]; snprintf(name, sizeof(name), "sampled_%d.i8", fs);
        sample_ref = open_in(argv[3], name); previous_fs = fs;
      }
      binary_check(sample_ref, sampled, length);
    }
    ++rows;
  }
  assert(rows == 16);
  if (sample_ref) { assert(fgetc(sample_ref) == EOF); fclose(sample_ref); }
  fclose(csv); free(sampled);
  assert(bds_b1c_generate_code(0, BDS_B1C_DATA, chips, sizeof(chips)) == -1);
  assert(bds_b1c_generate_code(64, BDS_B1C_DATA, chips, sizeof(chips)) == -1);
  assert(bds_b1c_generate_code(1, (bds_b1c_component_t)2, chips, sizeof(chips)) == -1);
  assert(bds_b1c_generate_primary_code(1, chips, 10229) == -1);
  assert(bds_b1c_generate_secondary_code(1, chips, 1799) == -1);
  assert(bds_b1c_sample_boc11(1, BDS_B1C_DATA, NAN, chips, sizeof(chips)) == -1);
  puts("PASS: 189 MATLAB code vectors, ICD secondary vector, 16 MATLAB sampled waveforms");
  return 0;
}

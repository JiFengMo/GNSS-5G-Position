#include "bds5g/b1c.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846
#define FS 4092000.0
#define PERIOD 40920U

static uint32_t state = UINT32_C(123456789);
static double uniform(void)
{
  state ^= state << 13; state ^= state >> 17; state ^= state << 5;
  return (state + 1.0) / 4294967297.0;
}
static double normal(void)
{
  return sqrt(-2 * log(uniform())) * cos(2 * PI * uniform());
}

static void signal(bds5g_cf32_t *samples, size_t phase, double doppler,
                    double data_weight, double pilot_weight, int flip, double noise)
{
  int8_t *data = malloc(PERIOD), *pilot = malloc(PERIOD); assert(data && pilot);
  assert(!bds_b1c_sample_boc11(23, BDS_B1C_DATA, FS, data, PERIOD));
  assert(!bds_b1c_sample_boc11(23, BDS_B1C_PILOT, FS, pilot, PERIOD));
  for (size_t k = 0; k < 2U * PERIOD; ++k) {
    size_t index = (k + PERIOD - phase) % PERIOD;
    double sign = flip && k < phase ? -1 : 1;
    double a = sign * data_weight * data[index], b = sign * pilot_weight * pilot[index];
    double angle = 2 * PI * (20000 + doppler) * k / FS;
    samples[k].i = (float)(a * cos(angle) - b * sin(angle) + noise * normal());
    samples[k].q = (float)(a * sin(angle) + b * cos(angle) + noise * normal());
  }
  free(data); free(pilot);
}

int main(void)
{
  bds5g_cf32_t *samples = calloc(2U * PERIOD, sizeof(*samples)); assert(samples);
  bds_b1c_acquisition_options_t o;
  bds_b1c_acquisition_defaults(&o);
  o.sample_rate_hz = FS; o.if_hz = 20000;
  o.doppler_min_hz = -200; o.doppler_max_hz = 200;
  int prns[2] = {23, 1}; bds_b1c_acquisition_t r[2];
  assert(!bds_b1c_acquire_many(samples, 2U * PERIOD, &o, prns, 2, r));
  assert(!r[0].acquired && !r[1].acquired && r[0].peak_metric == 0);
  assert(isnan(r[0].cn0_db_hz) && isnan(r[0].carrier_hz));
  const struct { size_t phase; double fd, data, pilot, integration; int flip; } cases[] = {
    {0, 0, 1, 0, 10, 0},
    {20000, 175, 0, 1, 10, 0},
    {PERIOD - 1, -175, .524404424, .851469318, 10, 1},
    {12001, 162.5, .524404424, .851469318, 1, 0},
    {4091, -125, .524404424, .851469318, 5, 0}
  };
  for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k) {
    signal(samples, cases[k].phase, cases[k].fd, cases[k].data, cases[k].pilot,
           cases[k].flip, .01);
    o.coherent_integration_ms = cases[k].integration;
    assert(!bds_b1c_acquire_many(samples, 2U * PERIOD, &o, prns, 1, r));
    fprintf(stderr, "case %zu: phase=%zu expected=%zu fd=%.1f expected=%.1f metric=%.2f\n",
            k, r[0].code_phase_samples, cases[k].phase, r[0].doppler_hz, cases[k].fd, r[0].peak_metric);
    assert(r[0].acquired);
    assert(r[0].code_phase_samples == cases[k].phase);
    assert(fabs(r[0].doppler_hz - cases[k].fd) <= 12.50001);
    assert(r[0].carrier_hz == o.if_hz + r[0].doppler_hz);
    assert(isnan(r[0].cn0_db_hz));
  }
  signal(samples, 0, 0, 0, 0, 0, 1);
  assert(!bds_b1c_acquire_many(samples, 2U * PERIOD, &o, prns, 2, r));
  assert(!r[0].acquired && !r[1].acquired);
  assert(bds_b1c_acquire_many(samples, 2U * PERIOD - 1U, &o, prns, 1, r) == -1);
  samples[0].q = NAN;
  assert(bds_b1c_acquire_many(samples, 2U * PERIOD, &o, prns, 1, r) == -1);
  samples[0].q = 0; o.doppler_step_hz = 0;
  assert(bds_b1c_acquire_many(samples, 2U * PERIOD, &o, prns, 1, r) == -1);
  o.doppler_step_hz = 50; o.sample_rate_hz = INFINITY;
  assert(bds_b1c_acquire_many(samples, 2U * PERIOD, &o, prns, 1, r) == -1);
  o.sample_rate_hz = FS; prns[1] = prns[0];
  assert(bds_b1c_acquire_many(samples, 2U * PERIOD, &o, prns, 2, r) == -1);
  free(samples);
  puts("PASS: full-period phase, data-only/pilot-only/joint, sign transition, off-grid Doppler,");
  puts("1/5/10 ms integration, direct-mix fallback, silence/noise and invalid inputs");
  return 0;
}

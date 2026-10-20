#include "bds5g/fusion.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double range_m(bds5g_vec3_t a, bds5g_vec3_t b)
{
  const double x = a.x - b.x;
  const double y = a.y - b.y;
  const double z = a.z - b.z;
  return sqrt(x * x + y * y + z * z);
}

int main(void)
{
  const bds5g_vec3_t truth = {1115000.0, -4843000.0, 3987000.0};
  const bds5g_vec3_t sources[] = {
      {15600000.0, 7540000.0, 20140000.0},
      {-18760000.0, 2750000.0, 18610000.0},
      {17610000.0, -14630000.0, 13480000.0},
      {19170000.0, 610000.0, -18390000.0},
      {-13400000.0, -21100000.0, 9200000.0},
      {truth.x - 300.0, truth.y - 100.0, truth.z + 20.0},
      {truth.x + 250.0, truth.y - 180.0, truth.z + 25.0},
      {truth.x + 120.0, truth.y + 310.0, truth.z + 18.0},
      {truth.x - 280.0, truth.y + 230.0, truth.z + 22.0}};
  bds5g_observation_t observations[9];
  bds5g_solution_t solution;
  const bds5g_vec3_t initial = {truth.x + 80.0,
                                truth.y - 60.0,
                                truth.z + 45.0};
  memset(observations, 0, sizeof(observations));
  for (size_t i = 0U; i < 9U; ++i) {
    observations[i].source = i < 5U ? BDS5G_SOURCE_BDS_B1C
                                     : BDS5G_SOURCE_NR_SSB;
    observations[i].source_id = (int)i;
    observations[i].source_ecef_m = sources[i];
    observations[i].pseudorange_m = range_m(truth, sources[i]) +
                                    (i < 5U ? 22.0 : -9.0);
    observations[i].sigma_m = i < 5U ? 1.5 : 0.8;
    observations[i].valid = 1U;
  }

  assert(bds5g_fusion_solve_wls(observations,
                                 9U,
                                 initial,
                                 NULL,
                                 &solution) == 0);
  assert(solution.converged);
  assert(range_m(solution.receiver_ecef_m, truth) < 0.05);
  assert(fabs(solution.bds_clock_bias_m - 22.0) < 0.05);
  assert(fabs(solution.nr_clock_bias_m + 9.0) < 0.05);
  puts("fusion WLS test passed");
  return 0;
}


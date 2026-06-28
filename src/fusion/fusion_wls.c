#include "bds5g/fusion.h"

#include <math.h>
#include <string.h>

#define WLS_MAX_STATE 5U

static int solve_linear(double matrix[WLS_MAX_STATE][WLS_MAX_STATE],
                        double vector[WLS_MAX_STATE],
                        size_t n,
                        double result[WLS_MAX_STATE])
{
  size_t col;
  for (col = 0U; col < n; ++col) {
    size_t pivot = col;
    size_t row;
    for (row = col + 1U; row < n; ++row) {
      if (fabs(matrix[row][col]) > fabs(matrix[pivot][col])) {
        pivot = row;
      }
    }
    if (fabs(matrix[pivot][col]) < 1.0e-12) {
      return -1;
    }
    if (pivot != col) {
      double temporary;
      size_t k;
      for (k = col; k < n; ++k) {
        temporary = matrix[col][k];
        matrix[col][k] = matrix[pivot][k];
        matrix[pivot][k] = temporary;
      }
      temporary = vector[col];
      vector[col] = vector[pivot];
      vector[pivot] = temporary;
    }
    for (row = col + 1U; row < n; ++row) {
      const double factor = matrix[row][col] / matrix[col][col];
      size_t k;
      for (k = col; k < n; ++k) {
        matrix[row][k] -= factor * matrix[col][k];
      }
      vector[row] -= factor * vector[col];
    }
  }

  for (col = n; col-- > 0U;) {
    double value = vector[col];
    size_t k;
    for (k = col + 1U; k < n; ++k) {
      value -= matrix[col][k] * result[k];
    }
    result[col] = value / matrix[col][col];
  }
  return 0;
}

static int is_nr_source(bds5g_source_t source)
{
  return source == BDS5G_SOURCE_NR_SSB || source == BDS5G_SOURCE_NR_PRS;
}

bds5g_wls_options_t bds5g_wls_default_options(void)
{
  bds5g_wls_options_t options;
  options.max_iterations = 15U;
  options.position_tolerance_m = 1.0e-3;
  options.huber_threshold_sigma = 3.0;
  return options;
}

int bds5g_fusion_solve_wls(const bds5g_observation_t *observations,
                           size_t observation_count,
                           bds5g_vec3_t initial_ecef_m,
                           const bds5g_wls_options_t *options,
                           bds5g_solution_t *solution)
{
  bds5g_wls_options_t local_options;
  double state[WLS_MAX_STATE] = {0.0, 0.0, 0.0, 0.0, 0.0};
  size_t valid_count = 0U;
  int has_bds = 0;
  int has_nr = 0;
  size_t state_count;
  unsigned iteration;

  if (!observations || !solution) {
    return -1;
  }
  local_options = options ? *options : bds5g_wls_default_options();
  if (local_options.max_iterations == 0U || local_options.position_tolerance_m <= 0.0) {
    return -1;
  }

  for (size_t i = 0U; i < observation_count; ++i) {
    if (!observations[i].valid || observations[i].sigma_m <= 0.0 ||
        observations[i].pseudorange_m <= 0.0) {
      continue;
    }
    ++valid_count;
    if (is_nr_source(observations[i].source)) {
      has_nr = 1;
    } else {
      has_bds = 1;
    }
  }
  state_count = (has_bds && has_nr) ? 5U : 4U;
  if (valid_count < state_count) {
    return -2;
  }

  memset(solution, 0, sizeof(*solution));
  state[0] = initial_ecef_m.x;
  state[1] = initial_ecef_m.y;
  state[2] = initial_ecef_m.z;

  for (iteration = 0U; iteration < local_options.max_iterations; ++iteration) {
    double normal[WLS_MAX_STATE][WLS_MAX_STATE] = {{0.0}};
    double rhs[WLS_MAX_STATE] = {0.0};
    double delta[WLS_MAX_STATE] = {0.0};
    size_t used = 0U;

    for (size_t i = 0U; i < observation_count; ++i) {
      double design[WLS_MAX_STATE] = {0.0};
      double dx;
      double dy;
      double dz;
      double geometric_range;
      double predicted;
      double residual;
      double normalized;
      double robust = 1.0;
      double weight;
      size_t bias_index = 3U;

      if (!observations[i].valid || observations[i].sigma_m <= 0.0 ||
          observations[i].pseudorange_m <= 0.0) {
        continue;
      }
      if (state_count == 5U && is_nr_source(observations[i].source)) {
        bias_index = 4U;
      }
      dx = state[0] - observations[i].source_ecef_m.x;
      dy = state[1] - observations[i].source_ecef_m.y;
      dz = state[2] - observations[i].source_ecef_m.z;
      geometric_range = sqrt(dx * dx + dy * dy + dz * dz);
      if (geometric_range < 1.0) {
        continue;
      }
      predicted = geometric_range + state[bias_index];
      residual = observations[i].pseudorange_m - predicted;
      normalized = fabs(residual) / observations[i].sigma_m;
      if (local_options.huber_threshold_sigma > 0.0 &&
          normalized > local_options.huber_threshold_sigma) {
        robust = local_options.huber_threshold_sigma / normalized;
      }
      weight = robust /
               (observations[i].sigma_m * observations[i].sigma_m);
      design[0] = dx / geometric_range;
      design[1] = dy / geometric_range;
      design[2] = dz / geometric_range;
      design[bias_index] = 1.0;

      for (size_t row = 0U; row < state_count; ++row) {
        rhs[row] += weight * design[row] * residual;
        for (size_t col = 0U; col < state_count; ++col) {
          normal[row][col] += weight * design[row] * design[col];
        }
      }
      ++used;
    }

    if (used < state_count ||
        solve_linear(normal, rhs, state_count, delta) != 0) {
      return -3;
    }
    for (size_t i = 0U; i < state_count; ++i) {
      state[i] += delta[i];
    }
    if (sqrt(delta[0] * delta[0] + delta[1] * delta[1] +
             delta[2] * delta[2]) < local_options.position_tolerance_m) {
      solution->converged = 1U;
      break;
    }
  }

  {
    double residual_sum = 0.0;
    size_t residual_count = 0U;
    for (size_t i = 0U; i < observation_count; ++i) {
      size_t bias_index = 3U;
      double dx;
      double dy;
      double dz;
      double range;
      double residual;
      if (!observations[i].valid) {
        continue;
      }
      if (state_count == 5U && is_nr_source(observations[i].source)) {
        bias_index = 4U;
      }
      dx = state[0] - observations[i].source_ecef_m.x;
      dy = state[1] - observations[i].source_ecef_m.y;
      dz = state[2] - observations[i].source_ecef_m.z;
      range = sqrt(dx * dx + dy * dy + dz * dz);
      residual = observations[i].pseudorange_m - (range + state[bias_index]);
      residual_sum += residual * residual;
      ++residual_count;
    }
    solution->receiver_ecef_m.x = state[0];
    solution->receiver_ecef_m.y = state[1];
    solution->receiver_ecef_m.z = state[2];
    solution->bds_clock_bias_m = state[3];
    solution->nr_clock_bias_m = (state_count == 5U) ? state[4] : state[3];
    solution->residual_rms_m = residual_count > 0U
        ? sqrt(residual_sum / (double)residual_count)
        : 0.0;
    solution->used_observations = residual_count;
  }
  return solution->converged ? 0 : 1;
}


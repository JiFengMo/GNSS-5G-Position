#include "bds5g/fusion.h"
#include "bds5g/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
  fprintf(stderr,
          "Usage: %s --observations FILE --initial-x M --initial-y M "
          "--initial-z M [--output FILE]\n",
          program);
}

int main(int argc, char **argv)
{
  const char *observation_path = NULL;
  const char *output_path = NULL;
  bds5g_vec3_t initial = {0.0, 0.0, 0.0};
  bds5g_observation_t observations[BDS5G_MAX_OBSERVATIONS];
  bds5g_solution_t solution;
  size_t observation_count = 0U;
  int have_x = 0;
  int have_y = 0;
  int have_z = 0;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--observations") == 0 && i + 1 < argc) {
      observation_path = argv[++i];
    } else if (strcmp(argv[i], "--initial-x") == 0 && i + 1 < argc) {
      initial.x = strtod(argv[++i], NULL);
      have_x = 1;
    } else if (strcmp(argv[i], "--initial-y") == 0 && i + 1 < argc) {
      initial.y = strtod(argv[++i], NULL);
      have_y = 1;
    } else if (strcmp(argv[i], "--initial-z") == 0 && i + 1 < argc) {
      initial.z = strtod(argv[++i], NULL);
      have_z = 1;
    } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
      output_path = argv[++i];
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  if (!observation_path || !have_x || !have_y || !have_z) {
    usage(argv[0]);
    return 2;
  }
  if (bds5g_load_observations_csv(observation_path,
                                   observations,
                                   BDS5G_MAX_OBSERVATIONS,
                                   &observation_count) != 0) {
    fprintf(stderr, "failed to load observations: %s\n", observation_path);
    return 3;
  }
  if (bds5g_fusion_solve_wls(observations,
                              observation_count,
                              initial,
                              NULL,
                              &solution) != 0) {
    fprintf(stderr, "position solver did not converge\n");
    return 4;
  }

  {
    FILE *stream = output_path ? fopen(output_path, "w") : stdout;
    if (!stream) {
      fprintf(stderr, "failed to open output file: %s\n", output_path);
      return 5;
    }
    fprintf(stream,
            "{\n"
            "  \"ecef_m\": [%.6f, %.6f, %.6f],\n"
            "  \"bds_clock_bias_m\": %.6f,\n"
            "  \"nr_clock_bias_m\": %.6f,\n"
            "  \"residual_rms_m\": %.6f,\n"
            "  \"used_observations\": %zu,\n"
            "  \"converged\": true\n"
            "}\n",
            solution.receiver_ecef_m.x,
            solution.receiver_ecef_m.y,
            solution.receiver_ecef_m.z,
            solution.bds_clock_bias_m,
            solution.nr_clock_bias_m,
            solution.residual_rms_m,
            solution.used_observations);
    if (stream != stdout) {
      fclose(stream);
    }
  }
  return 0;
}


#include "bds5g/io.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int bds5g_load_observations_csv(const char *path,
                                bds5g_observation_t *observations,
                                size_t capacity,
                                size_t *count)
{
  FILE *stream;
  char line[1024];
  size_t loaded = 0U;

  if (!path || !observations || !count || capacity == 0U) {
    return -1;
  }
  stream = fopen(path, "r");
  if (!stream) {
    return -2;
  }

  while (fgets(line, sizeof(line), stream)) {
    bds5g_observation_t observation;
    unsigned source;
    unsigned valid;
    int fields;
    if (line[0] == '#' || line[0] == '\n' ||
        strncmp(line, "timestamp_ns", 12U) == 0) {
      continue;
    }
    memset(&observation, 0, sizeof(observation));
    fields = sscanf(line,
                    "%" SCNu64 ",%u,%d,%lf,%lf,%lf,%lf,%lf,%lf,%u",
                    &observation.timestamp_ns,
                    &source,
                    &observation.source_id,
                    &observation.source_ecef_m.x,
                    &observation.source_ecef_m.y,
                    &observation.source_ecef_m.z,
                    &observation.pseudorange_m,
                    &observation.sigma_m,
                    &observation.cn0_db_hz,
                    &valid);
    if (fields != 10) {
      fclose(stream);
      return -3;
    }
    if (loaded >= capacity || source > (unsigned)BDS5G_SOURCE_NR_PRS) {
      fclose(stream);
      return -4;
    }
    observation.source = (bds5g_source_t)source;
    observation.valid = valid ? 1U : 0U;
    observations[loaded++] = observation;
  }
  fclose(stream);
  *count = loaded;
  return loaded > 0U ? 0 : -5;
}

int bds5g_append_observation_csv(const char *path,
                                 const bds5g_observation_t *observation,
                                 int write_header)
{
  FILE *stream;
  if (!path || !observation) {
    return -1;
  }
  stream = fopen(path, write_header ? "w" : "a");
  if (!stream) {
    return -2;
  }
  if (write_header) {
    fputs("timestamp_ns,source,source_id,source_x_m,source_y_m,source_z_m,"
          "pseudorange_m,sigma_m,cn0_db_hz,valid\n",
          stream);
  }
  fprintf(stream,
          "%" PRIu64 ",%u,%d,%.6f,%.6f,%.6f,%.9f,%.6f,%.3f,%u\n",
          observation->timestamp_ns,
          (unsigned)observation->source,
          observation->source_id,
          observation->source_ecef_m.x,
          observation->source_ecef_m.y,
          observation->source_ecef_m.z,
          observation->pseudorange_m,
          observation->sigma_m,
          observation->cn0_db_hz,
          (unsigned)observation->valid);
  fclose(stream);
  return 0;
}


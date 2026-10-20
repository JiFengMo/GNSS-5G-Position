#pragma once

#include "bds5g/types.h"

#ifdef __cplusplus
extern "C" {
#endif

int bds5g_load_observations_csv(const char *path,
                                bds5g_observation_t *observations,
                                size_t capacity,
                                size_t *count);

int bds5g_append_observation_csv(const char *path,
                                 const bds5g_observation_t *observation,
                                 int write_header);

#ifdef __cplusplus
}
#endif


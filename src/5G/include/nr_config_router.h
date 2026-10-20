#pragma once
#include "nr_pos_types.h"
#ifndef NR_DEFAULT_MAIN_CONFIG
#define NR_DEFAULT_MAIN_CONFIG "config/config.f"
#endif
/* role is receiver or anchor. Paths are relative to the owning config. */
int nr_toa_load_main_config(const char *main_path, const char *role, nr_toa_app_cfg_t *cfg);

#include "nr_config_router.h"
#include "nr_pos_api.h"
#include <stdio.h>
int main(int argc, char **argv)
{
  nr_toa_app_cfg_t cfg;
  nr_anchor_desc_t anchors[NR_TOA_MAX_ANCHORS];
  int count = 0;
  const char *main_path = argc > 1 ? argv[1] : NR_DEFAULT_MAIN_CONFIG;
  const char *role = argc > 2 ? argv[2] : "receiver";
  if (nr_toa_load_main_config(main_path, role, &cfg)) return 1;
  if (cfg.anchor_db_path[0] && nr_toa_load_anchor_db(cfg.anchor_db_path, anchors, &count)) {
    fprintf(stderr, "Cannot load anchor database: %s\n", cfg.anchor_db_path); return 2;
  }
  printf("config OK: sdr=%s center_freq_hz=%.0f sample_rate_hz=%.0f target_pci=%d target_ssb_index=%d anchors=%d\n",
         cfg.sdr, cfg.center_freq_hz, cfg.sample_rate_hz, cfg.target_pci, cfg.target_ssb_index, count);
  return 0;
}

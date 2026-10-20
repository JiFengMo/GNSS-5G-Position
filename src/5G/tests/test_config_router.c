#include "nr_config_router.h"
#include "nr_pos_api.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MAKE_DIR(p) mkdir(p, 0700)
#endif

static int put_file(const char *path, const char *text)
{
  FILE *fp = fopen(path, "w");
  if (!fp) return 1;
  if (fputs(text, fp) == EOF) { fclose(fp); return 1; }
  return fclose(fp) != 0;
}
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "config test failed: line %d\n", __LINE__); return 1; } } while (0)

int main(void)
{
  nr_toa_app_cfg_t cfg;
  nr_anchor_desc_t anchors[NR_TOA_MAX_ANCHORS];
  int count = 0;
  (void)MAKE_DIR("router-fixture");
  (void)MAKE_DIR("router-fixture/profiles");
  CHECK(!put_file("router-fixture/profiles/anchors.csv",
    "anchor_id,pci,ssb_index,x_m,y_m,z_m,absolute_time_valid,hw_cal_delay_ns\n0,84,1,1,2,3,1,4\n"));
  CHECK(!put_file("router-fixture/profiles/rx.f",
    "sdr = usrp\nprovider = software_ssb\ncenter_freq_hz = 2114550000\nsample_rate_hz = 30720000\ntarget_pci = 84\nanchor_db_path = anchors.csv\n"));
  CHECK(!put_file("router-fixture/profiles/tx.f", "sdr = usrp\ntx_gain_db = 40\n"));
  CHECK(!put_file("router-fixture/main.f",
    "enable_5g = 1\nnr_config = profiles/rx.f\nnr_anchor_config = profiles/tx.f\nenable_gnss = 0\ngnss_config = missing.f\nenable_fusion = 0\nfusion_config = missing.f\n"));
  CHECK(!nr_toa_load_main_config("router-fixture/main.f", "receiver", &cfg));
  CHECK(cfg.center_freq_hz == 2114550000.0 && cfg.target_pci == 84);
  CHECK(cfg.target_ssb_index == -1); /* preserve old loader defaults */
  CHECK(!nr_toa_load_anchor_db(cfg.anchor_db_path, anchors, &count) && count == 1);
  CHECK(!nr_toa_load_main_config("router-fixture/main.f", "anchor", &cfg) && cfg.tx_gain_db == 40.0);
  CHECK(!put_file("router-fixture/main.f", "enable_5g = 0\nnr_config = profiles/rx.f\n"));
  CHECK(nr_toa_load_main_config("router-fixture/main.f", "receiver", &cfg) != 0);
  CHECK(!put_file("router-fixture/main.f", "enable_5g = 1\nnr_config = missing.f\n"));
  CHECK(nr_toa_load_main_config("router-fixture/main.f", "receiver", &cfg) != 0);
  CHECK(!put_file("router-fixture/main.f", "enable_5g = 1\nenable_5g = 1\nnr_config = profiles/rx.f\n"));
  CHECK(nr_toa_load_main_config("router-fixture/main.f", "receiver", &cfg) != 0);
  CHECK(!put_file("router-fixture/main.f", "enable_5g = 1\nwrong_key = profiles/rx.f\n"));
  CHECK(nr_toa_load_main_config("router-fixture/main.f", "receiver", &cfg) != 0);
  CHECK(!put_file("router-fixture/main.f", "enable_5g = 1\nnr_config = profiles/rx.f\n"));
  CHECK(!put_file("router-fixture/profiles/rx.f", "provider = unavailable\n"));
  CHECK(nr_toa_load_main_config("router-fixture/main.f", "receiver", &cfg) != 0);
  puts("main/child config routing tests passed");
  return 0;
}

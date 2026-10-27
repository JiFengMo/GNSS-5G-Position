#include "nr_monitor.h"
#include "nr_log_paths.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"monitor test failed at %d\n",__LINE__); return 1; } } while (0)
int main(void)
{
  FILE *fp = fopen("monitor-test.f", "w"); CHECK(fp);
  struct timeval started; gettimeofday(&started, NULL);
  char relative_dir[160]; snprintf(relative_dir,sizeof(relative_dir),"tests/monitor/%ld-%ld",(long)started.tv_sec,(long)started.tv_usec);
  fputs("enabled = 1\nstatus_interval_ms = 200\ntrace_interval_ms = 500\n",fp);
  fprintf(fp,"record_dir = %s\n",relative_dir);
  fputs("gnss_record_dir = gnss-ui\n",fp);
  fputs("segment_mb = 1\nmax_segments = 2\nsource_mode = controlled\ntime_model = shared\nabsolute_toa_calibrated = 0\n", fp); fclose(fp);
  nr_toa_app_cfg_t cfg; memset(&cfg, 0, sizeof(cfg)); cfg.sample_rate_hz = 30720000; cfg.center_freq_hz = 3600000000;
  strcpy(cfg.sdr_addrs, "serial=example\"\\test");
  CHECK(!nr_monitor_start("monitor-test.f", &cfg));
  c16_t iq[64]; for (unsigned i = 0; i < 64; ++i) { iq[i].r = (int16_t)i; iq[i].i = -(int16_t)i; }
  nr_iq_block_t block; memset(&block,0,sizeof(block)); block.rx[0] = iq; block.nsamps=64; block.fs_hz=cfg.sample_rate_hz;
  block.rx_freq_hz=cfg.center_freq_hz; block.ts_first=UINT64_C(10000000000000001); block.target_pci=84;
  nr_sync_state_t sync; memset(&sync,0,sizeof(sync)); sync.pci=84; sync.locked=sync.pbch_ok=sync.pbch_confirmed=sync.mib_ok=1; sync.cfo_hz=NAN;
  nr_clock_status_t clock; memset(&clock,0,sizeof(clock)); clock.locked=1;
  nr_monitor_begin(&block, 1); CHECK(nr_monitor_capturing());
  for (unsigned i=0;i<64;i+=4) nr_monitor_pss(i,0,(double)i,64,4);
  cf32_t bins[1024]; memset(bins,0,sizeof(bins)); bins[513].r=20;
  nr_cir_t cir; memset(&cir,0,sizeof(cir)); cir.cir=bins; cir.cir_len=1024; cir.bin_width_s=1.0/30720000;
  nr_monitor_cir(&cir,513,.125); nr_monitor_end(&block,&sync,&clock);
  /* A retune must not bypass the trace interval. */
  block.rx_freq_hz+=10000000; nr_monitor_begin(&block,2); CHECK(!nr_monitor_capturing());
  nr_monitor_stop();
  char directory[4096]; CHECK(!nr_log_directory(directory,sizeof(directory),relative_dir,NULL));
  DIR *dir=opendir(directory); CHECK(dir); struct dirent *entry; int found=0;
  while ((entry=readdir(dir))) {
    if (!strstr(entry->d_name,".jsonl")) continue;
    char path[4352]; snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name);
    fp=fopen(path,"r"); CHECK(fp); char *text=(char *)calloc(1,100000); CHECK(text); size_t n=fread(text,1,99999,fp); fclose(fp);text[n]=0;
    if (strstr(text,"10000000000000001")) {
      CHECK(strstr(text,"\"cfo_hz\":null"));
      CHECK(strstr(text,"\"clock_locked\":true"));
      CHECK(strstr(text,"\"cir_peak_sample\":513"));
      CHECK(strstr(text,"\"fractional_offset_samp\":0.125"));
      CHECK(strstr(text,"\"cir_bin_width_ns\":32.5520833"));
      CHECK(strstr(text,"\"cir_magnitude\"")); CHECK(strstr(text,"serial=example\\\"\\\\test")); ++found;
      printf("recording=%s\n",path);
    }
    free(text);
  }
  closedir(dir); CHECK(found>0); remove("monitor-test.f");
  puts("bounded monitor recording, numeric JSON and peak retention passed"); return 0;
}

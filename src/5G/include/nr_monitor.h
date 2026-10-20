#pragma once
#include "nr_pos_types.h"
#include "nr_toa_ue.h"

/* Optional observation only. Calls never wait for disk or the browser. */
int nr_monitor_start(const char *path, const nr_toa_app_cfg_t *cfg);
void nr_monitor_stop(void);
void nr_monitor_begin(const nr_iq_block_t *blk, uint64_t job_id);
int nr_monitor_capturing(void);
void nr_monitor_pss(uint32_t sample, int nid2, double power, uint32_t total, uint32_t step);
void nr_monitor_channel(const nr_chest_full_t *channel);
void nr_monitor_cir(const nr_cir_t *cir, int peak, double fraction);
void nr_monitor_meas(const nr_toa_meas_t *meas);
void nr_monitor_end(const nr_iq_block_t *blk, const nr_sync_state_t *sync, const nr_clock_status_t *clock);
void nr_monitor_runtime(const nr_toa_ue_t *ue);

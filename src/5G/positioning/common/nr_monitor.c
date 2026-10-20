#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include "nr_monitor.h"
#include "nr_log_paths.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define POINTS 512U
#define CURVES 9U
#define QUEUE 32U
typedef struct { uint32_t n, original; float x[POINTS], y[POINTS]; } curve_t;
typedef struct {
  int kind, detailed, has_meas;
  double time_ms, freq, fs;
  uint64_t job, ts, abs_sample;
  uint32_t nsamps, pss_step;
  int cir_peak;
  double fraction;
  nr_sync_state_t sync;
  nr_toa_meas_t meas;
  curve_t curves[CURVES];
  int state, clock, ref, pps, gps;
  uint64_t reads, gaps, sync_dropped;
  uint32_t overruns;
} event_t;
static struct {
  int enabled, stop, shared, calibrated;
  unsigned status_ms, trace_ms, segment_mb, segments;
  volatile unsigned long dropped, io_errors;
  double next_runtime;
  char directory[4096], run[96], source[32], model[32];
  nr_toa_app_cfg_t cfg;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t ready;
  unsigned head, count;
  event_t queue[QUEUE];
} monitor;
static __thread struct {
  event_t event;
  int active, status_due;
  struct { double freq, next_status, next_trace; int target; } limits[NR_TOA_MAX_SCAN_TARGETS];
} capture;

static double now_ms(void)
{
  struct timeval t; gettimeofday(&t, NULL);
  return t.tv_sec * 1000.0 + t.tv_usec / 1000.0;
}
static char *trim(char *s)
{
  char *e; while (*s == ' ' || *s == '\t') ++s;
  e = s + strlen(s); while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) --e;
  *e = 0; return s;
}
static void json_string(FILE *fp, const char *s)
{
  fputc('"', fp);
  for (; *s; ++s) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { fputc('\\', fp); fputc(c, fp); }
    else if (c < 32) fprintf(fp, "\\u%04x", c);
    else fputc(c, fp);
  }
  fputc('"', fp);
}
static void number(FILE *fp, double v)
{
  if (isfinite(v)) fprintf(fp, "%.9g", v); else fputs("null", fp);
}
static void write_config(FILE *fp)
{
  fprintf(fp, "{\"schema\":1,\"kind\":\"config\",\"run_id\":\"%s\",\"time_ms\":%.3f,\"source_mode\":", monitor.run, now_ms());
  json_string(fp, monitor.source); fputs(",\"time_model\":", fp); json_string(fp, monitor.model);
  fprintf(fp, ",\"absolute_toa_calibrated\":%s,\"status_interval_ms\":%u,\"trace_interval_ms\":%u,\"configured\":{\"center_freq_hz\":%.0f,\"sample_rate_hz\":%.0f,\"rx_gain_db\":%.3f,\"target_pci\":%d,\"target_ssb_index\":%d,\"scan_target_count\":%u,\"clock_source\":",
          monitor.calibrated ? "true" : "false", monitor.status_ms, monitor.trace_ms,
          monitor.cfg.center_freq_hz, monitor.cfg.sample_rate_hz, monitor.cfg.rx_gain_db,
          monitor.cfg.target_pci, monitor.cfg.target_ssb_index, monitor.cfg.scan_target_count);
  json_string(fp, monitor.cfg.clock_source); fputs(",\"time_source\":", fp); json_string(fp, monitor.cfg.time_source);
  fputs(",\"provider\":", fp); json_string(fp, monitor.cfg.provider);
  fputs(",\"sdr\":", fp); json_string(fp, monitor.cfg.sdr);
  fputs(",\"sdr_addrs\":", fp); json_string(fp, monitor.cfg.sdr_addrs);
  fputs(",\"anchor_db_path\":", fp); json_string(fp, monitor.cfg.anchor_db_path);
  fputs("},\"position\":{\"valid\":false,\"reason\":\"not_implemented\",\"xyz_m\":null}}\n", fp);
}
static void write_event(FILE *fp, const event_t *e, unsigned long seq)
{
  static const char *names[CURVES] = {"iq_i", "iq_q", "pss_0", "pss_1", "pss_2", "channel_magnitude", "channel_phase", "cir_magnitude", "fractional_peak"};
  const nr_sync_state_t *s = &e->sync;
  const int locked = s->locked && s->pbch_ok && s->pbch_confirmed && s->mib_ok;
  fprintf(fp, "{\"schema\":1,\"kind\":\"%s\",\"run_id\":\"%s\",\"seq\":%lu,\"time_ms\":%.3f,\"dropped\":%lu,\"io_errors\":%lu,",
          e->kind ? "runtime" : "sample", monitor.run, seq, e->time_ms, monitor.dropped, monitor.io_errors);
  if (e->kind) {
    fprintf(fp, "\"state\":%d,\"clock_locked\":%s,\"ref_locked\":%s,\"pps_locked\":%s,\"gps_locked\":%s,\"sample_rate_hz\":%.0f,\"capture_freq_hz\":%.0f,\"rx_reads\":%llu,\"rx_gaps\":%llu,\"sync_dropped\":%llu,\"ring_overruns\":%u}\n",
            e->state, e->clock ? "true" : "false", e->ref ? "true" : "false", e->pps ? "true" : "false", e->gps ? "true" : "false", e->fs, e->freq,
            (unsigned long long)e->reads, (unsigned long long)e->gaps, (unsigned long long)e->sync_dropped, e->overruns);
    return;
  }
  fprintf(fp, "\"event_id\":\"%s:%llu\",\"hardware_sample\":\"%llu\",\"abs_sample\":\"%llu\",\"freq_hz\":%.0f,\"sample_rate_hz\":%.0f,\"nsamps\":%u,\"pci\":%u,\"ssb\":%u,\"locked\":%s,\"mib_ok\":%s,\"pbch_ok\":%s,\"fail_stage\":%u,\"coarse_offset_samp\":%d,\"tracking_shift_samp\":%lld,\"pss_step_samp\":%u,\"cfo_hz\":",
          monitor.run, (unsigned long long)e->job, (unsigned long long)e->ts, (unsigned long long)e->abs_sample, e->freq, e->fs, e->nsamps,
          s->pci, s->ssb_index, locked ? "true" : "false", s->mib_ok ? "true" : "false", s->pbch_ok ? "true" : "false", s->pbch_fail_stage,
          s->coarse_offset_samp, (long long)s->cum_tracking_shift_samp, e->pss_step);
  number(fp, s->cfo_hz); fputs(",\"snr_db\":", fp); number(fp, s->snr_db);
  fputs(",\"pss_metric\":", fp); number(fp, s->pss_metric);
  fprintf(fp, ",\"clock_locked\":%s,\"cir_peak_sample\":", e->clock ? "true" : "false");
  if (e->curves[7].n) fprintf(fp, "%d", e->cir_peak); else fputs("null", fp);
  fputs(",\"fractional_offset_samp\":", fp);
  if (e->curves[7].n) number(fp, e->fraction); else fputs("null", fp);
  fputs(",\"relative_delay_ns\":", fp);
  if (e->has_meas && e->meas.fs_hz > 0 && locked)
    number(fp, ((double)((int64_t)e->meas.rx_ts_int - (int64_t)e->ts) + e->meas.rx_ts_frac) * 1e9 / e->meas.fs_hz);
  else fputs("null", fp);
  /* Absolute time calibration is a separate, explicitly asserted prerequisite. */
  fputs(",\"absolute_candidate_valid\":", fp); fputs(e->has_meas && e->meas.valid ? "true" : "false", fp);
  fputs(",\"toa_candidate_ns\":", fp); if (e->has_meas && e->meas.valid) number(fp, e->meas.toa_ns); else fputs("null", fp);
  fputs(",\"quality\":", fp); if (e->has_meas) number(fp, e->meas.quality); else fputs("null", fp);
  fputs(",\"curves\":{", fp);
  int first = 1;
  for (unsigned k = 0; k < CURVES; ++k) {
    const curve_t *c = &e->curves[k]; if (!c->n) continue;
    if (!first) fputc(',', fp);
    first = 0; json_string(fp, names[k]);
    fprintf(fp, ":{\"original_points\":%u,\"x\":[", c->original);
    for (unsigned j = 0; j < c->n; ++j) { if (j) fputc(',', fp); number(fp, c->x[j]); }
    fputs("],\"y\":[", fp);
    for (unsigned j = 0; j < c->n; ++j) { if (j) fputc(',', fp); number(fp, c->y[j]); }
    fputs("]}", fp);
  }
  fputs("}}\n", fp);
}
static FILE *open_segment(unsigned segment)
{
  char path[4352];
  snprintf(path, sizeof(path), "%s/%s-%03u.jsonl", monitor.directory, monitor.run, segment % monitor.segments);
  FILE *fp = fopen(path, "w");
  if (fp) { write_config(fp); fflush(fp); }
  return fp;
}
static void *writer(void *unused)
{
  (void)unused;
  event_t *e = (event_t *)malloc(sizeof(event_t));
  unsigned segment = 0; unsigned long seq = 0;
  FILE *fp = open_segment(segment);
  if (!e || !fp) __sync_fetch_and_add(&monitor.io_errors, 1);
  for (;;) {
    pthread_mutex_lock(&monitor.mutex);
    while (!monitor.count && !monitor.stop) pthread_cond_wait(&monitor.ready, &monitor.mutex);
    if (!monitor.count && monitor.stop) { pthread_mutex_unlock(&monitor.mutex); break; }
    if (e) *e = monitor.queue[monitor.head];
    monitor.head = (monitor.head + 1U) % QUEUE; --monitor.count;
    pthread_mutex_unlock(&monitor.mutex);
    if (!e || !fp) continue;
    write_event(fp, e, ++seq);
    if (fflush(fp) || ferror(fp)) {
      __sync_fetch_and_add(&monitor.io_errors, 1); fclose(fp); fp = NULL;
      fprintf(stderr, "monitor: recording failed; receiver continues without recording\n");
      continue;
    }
    long bytes = ftell(fp);
    if (bytes >= 0 && (unsigned long)bytes >= monitor.segment_mb * 1024UL * 1024UL) {
      fclose(fp); fp = open_segment(++segment);
      if (!fp) __sync_fetch_and_add(&monitor.io_errors, 1);
    }
  }
  if (fp) fclose(fp);
  free(e); return NULL;
}
static void enqueue(const event_t *event)
{
  if (!monitor.enabled) return;
  if (pthread_mutex_trylock(&monitor.mutex)) { __sync_fetch_and_add(&monitor.dropped, 1); return; }
  if (monitor.count == QUEUE) {
    __sync_fetch_and_add(&monitor.dropped, 1); pthread_mutex_unlock(&monitor.mutex); return;
  }
  monitor.queue[(monitor.head + monitor.count) % QUEUE] = *event; ++monitor.count;
  pthread_cond_signal(&monitor.ready); pthread_mutex_unlock(&monitor.mutex);
}
void nr_monitor_stop(void)
{
  if (!monitor.enabled) return;
  pthread_mutex_lock(&monitor.mutex); monitor.stop = 1; pthread_cond_signal(&monitor.ready); pthread_mutex_unlock(&monitor.mutex);
  pthread_join(monitor.thread, NULL); monitor.enabled = 0;
  pthread_cond_destroy(&monitor.ready); pthread_mutex_destroy(&monitor.mutex);
}
int nr_monitor_start(const char *path, const nr_toa_app_cfg_t *cfg)
{
  char line[1024], directory[512] = "monitor";
  int enabled = 1;
  if (!path || !*path) return 0; /* old master configs retain previous behavior */
  if (monitor.enabled || !cfg) return -1;
  FILE *fp = fopen(path, "r"); if (!fp) return -1;
  memset(&monitor, 0, sizeof(monitor)); memset(&capture, 0, sizeof(capture));
  monitor.status_ms = 200; monitor.trace_ms = 1000; monitor.segment_mb = 128; monitor.segments = 8;
  strcpy(monitor.source, "controlled"); strcpy(monitor.model, "shared");
  while (fgets(line, sizeof(line), fp)) {
    char *key = line;
    if (strlen(line) >= 3 && (unsigned char)line[0] == 0xef && (unsigned char)line[1] == 0xbb && (unsigned char)line[2] == 0xbf) key += 3;
    key = trim(key); char *eq = strchr(key, '=');
    if (*key == '#' || !*key) continue;
    if (!eq) { fclose(fp); return -1; } *eq = 0; key = trim(key); char *value = trim(eq + 1);
    if (!*value) goto invalid;
    char *end; unsigned long v = strtoul(value, &end, 10);
    if (!strcmp(key, "enabled")) { if (*end || v > 1) goto invalid; enabled = (int)v; }
    else if (!strcmp(key, "status_interval_ms")) { if (*end || v < 100 || v > 60000) goto invalid; monitor.status_ms = (unsigned)v; }
    else if (!strcmp(key, "trace_interval_ms")) { if (*end || v < 500 || v > 60000) goto invalid; monitor.trace_ms = (unsigned)v; }
    else if (!strcmp(key, "segment_mb")) { if (*end || v < 1 || v > 512) goto invalid; monitor.segment_mb = (unsigned)v; }
    else if (!strcmp(key, "max_segments")) { if (*end || v < 2 || v > 64) goto invalid; monitor.segments = (unsigned)v; }
    else if (!strcmp(key, "record_dir")) { if (!*value || strlen(value) >= sizeof(directory)) goto invalid; strcpy(directory, value); }
    else if (!strcmp(key, "source_mode")) { if (strcmp(value,"controlled") && strcmp(value,"commercial")) goto invalid; strcpy(monitor.source, value); }
    else if (!strcmp(key, "time_model")) { if (strcmp(value,"shared") && strcmp(value,"receiver_bias")) goto invalid; strcpy(monitor.model, value); }
    else if (!strcmp(key, "absolute_toa_calibrated")) { if (*end || v > 1) goto invalid; monitor.calibrated = (int)v; }
    else if (strcmp(key,"port") && strcmp(key,"poll_interval_ms") && strcmp(key,"max_history") && strcmp(key,"stale_after_ms")) goto invalid;
  }
  fclose(fp); if (!enabled) return 0;
  if (nr_log_directory(monitor.directory, sizeof(monitor.directory), directory, "monitor")) return -1;
  monitor.shared = !strcmp(monitor.model, "shared"); monitor.cfg = *cfg;
  snprintf(monitor.run, sizeof(monitor.run), "run-%.0f-%ld", now_ms(), (long)getpid());
  if (pthread_mutex_init(&monitor.mutex, NULL)) return -1;
  if (pthread_cond_init(&monitor.ready, NULL)) { pthread_mutex_destroy(&monitor.mutex); return -1; }
  monitor.enabled = 1;
  if (pthread_create(&monitor.thread, NULL, writer, NULL)) {
    monitor.enabled = 0; pthread_cond_destroy(&monitor.ready); pthread_mutex_destroy(&monitor.mutex); return -1;
  }
  atexit(nr_monitor_stop);
  printf("monitor: run=%s directory=%s status_ms=%u trace_ms=%u\n", monitor.run, monitor.directory, monitor.status_ms, monitor.trace_ms);
  return 0;
invalid:
  fprintf(stderr, "monitor: invalid option in %s\n", path); fclose(fp); return -1;
}
void nr_monitor_begin(const nr_iq_block_t *blk, uint64_t job_id)
{
  capture.active = 0; capture.status_due = 0;
  if (!monitor.enabled || !blk) return;
  double t = now_ms(); unsigned slot = blk->scan_target_idx % NR_TOA_MAX_SCAN_TARGETS;
  if (capture.limits[slot].freq != blk->rx_freq_hz || capture.limits[slot].target != blk->target_pci) {
    capture.limits[slot].freq = blk->rx_freq_hz; capture.limits[slot].target = blk->target_pci;
    /* Do not reset deadlines on retunes: a wide sweep must stay rate limited. */
  }
  capture.status_due = t >= capture.limits[slot].next_status;
  capture.active = t >= capture.limits[slot].next_trace;
  if (!capture.status_due && !capture.active) return;
  memset(&capture.event, 0, sizeof(capture.event));
  capture.event.detailed = capture.active; capture.event.time_ms = t; capture.event.job = job_id;
  capture.event.ts = blk->ts_first; capture.event.abs_sample = blk->abs_samp0;
  capture.event.fs = blk->fs_hz; capture.event.freq = blk->rx_freq_hz; capture.event.nsamps = blk->nsamps;
  if (capture.status_due) capture.limits[slot].next_status = t + monitor.status_ms;
  if (capture.active) capture.limits[slot].next_trace = t + monitor.trace_ms;
}
int nr_monitor_capturing(void) { return monitor.enabled && capture.active; }
void nr_monitor_pss(uint32_t sample, int nid2, double power, uint32_t total, uint32_t step)
{
  if (!nr_monitor_capturing() || nid2 < 0 || nid2 > 2 || !total) return;
  curve_t *c = &capture.event.curves[2 + nid2];
  unsigned bin = (unsigned)((uint64_t)sample * POINTS / total); if (bin >= POINTS) bin = POINTS - 1;
  if (!c->n) for (unsigned i = 0; i < POINTS; ++i) c->y[i] = NAN;
  if (!isfinite(c->y[bin]) || power > c->y[bin]) { c->y[bin] = (float)power; c->x[bin] = (float)sample; }
  if (c->n <= bin) c->n = bin + 1;
  c->original = sample / step + 1U;
  capture.event.pss_step = step;
}
void nr_monitor_channel(const nr_chest_full_t *h)
{
  if (!nr_monitor_capturing() || !h || !h->h_full || h->n_re < 480) return;
  /* PBCH symbol 1, with its own subcarrier axis; never join different symbols. */
  curve_t *mag = &capture.event.curves[5], *phase = &capture.event.curves[6];
  for (unsigned i = 0; i < 240; ++i) {
    cf32_t v = h->h_full[240+i];
    mag->x[i] = phase->x[i] = (float)i;
    int valid = !h->valid_re || h->valid_re[240+i];
    mag->y[i] = valid ? hypotf(v.r,v.i) : NAN;
    phase->y[i] = valid ? atan2f(v.i,v.r) : NAN;
  }
  mag->n = phase->n = mag->original = phase->original = 240;
}
void nr_monitor_cir(const nr_cir_t *cir, int peak, double fraction)
{
  if (!nr_monitor_capturing() || !cir || !cir->cir || !cir->cir_len) return;
  curve_t *c = &capture.event.curves[7], *fit = &capture.event.curves[8];
  unsigned bins = cir->cir_len < POINTS ? cir->cir_len : POINTS;
  for (unsigned i = 0; i < bins; ++i) {
    unsigned a = (uint64_t)i * cir->cir_len / bins, b = (uint64_t)(i+1) * cir->cir_len / bins;
    float best = -1; unsigned pos = a;
    for (unsigned j = a; j < b; ++j) { float v = hypotf(cir->cir[j].r,cir->cir[j].i); if (v > best) { best = v; pos = j; } }
    c->x[i] = (float)pos; c->y[i] = best;
  }
  c->n = bins; c->original = cir->cir_len;
  if (peak > 0 && (unsigned)(peak+1) < cir->cir_len) {
    for (int i = -1; i <= 1; ++i) { cf32_t v = cir->cir[peak+i]; fit->x[i+1] = (float)(peak+i); fit->y[i+1] = hypotf(v.r,v.i); }
    fit->n = fit->original = 3;
  }
  capture.event.cir_peak = peak; capture.event.fraction = fraction;
}
void nr_monitor_meas(const nr_toa_meas_t *m)
{
  if (monitor.enabled && (capture.active || capture.status_due) && m && m->fs_hz > 0) { capture.event.meas = *m; capture.event.has_meas = 1; }
}
void nr_monitor_end(const nr_iq_block_t *blk, const nr_sync_state_t *sync, const nr_clock_status_t *clock)
{
  if (!monitor.enabled || !blk || !sync || (!capture.active && !capture.status_due)) return;
  capture.event.sync = *sync;
  capture.event.clock = clock ? clock->locked : 0;
  if (capture.active && blk->rx[0]) {
    unsigned start = sync->coarse_offset_samp > 256 ? (unsigned)sync->coarse_offset_samp - 256 : 0;
    if (start >= blk->nsamps) start = 0;
    unsigned len = blk->nsamps - start; if (len > 2048) len = 2048;
    unsigned n = len < POINTS ? len : POINTS;
    curve_t *i = &capture.event.curves[0], *q = &capture.event.curves[1];
    for (unsigned j = 0; j < n; ++j) { unsigned pos = start + (uint64_t)j * len / n; i->x[j] = q->x[j] = (float)pos; i->y[j] = blk->rx[0][pos].r; q->y[j] = blk->rx[0][pos].i; }
    i->n = q->n = n; i->original = q->original = len;
  }
  enqueue(&capture.event); capture.active = capture.status_due = 0;
}
void nr_monitor_runtime(const nr_toa_ue_t *ue)
{
  if (!monitor.enabled || !ue) return;
  double t = now_ms(); if (t < monitor.next_runtime) return; monitor.next_runtime = t + monitor.status_ms;
  event_t e; memset(&e, 0, sizeof(e)); e.kind = 1; e.time_ms = t; e.state = ue->state;
  e.clock = ue->clk.locked; e.ref = ue->clk.ref_locked; e.pps = ue->clk.pps_locked; e.gps = ue->clk.gps_locked;
  e.fs = ue->rf_cfg.sample_rate; e.freq = ue->current_rx_freq_hz;
  e.reads = ue->rx_read_count; e.gaps = ue->rx_gap_count; e.sync_dropped = ue->sync_jobs_dropped; e.overruns = ue->iq_ring.overrun_cnt;
  enqueue(&e);
}

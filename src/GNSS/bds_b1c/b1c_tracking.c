/* SPDX-License-Identifier: GPL-2.0-or-later
 * Narrowband B1C tracking adapted from CU-SDR by Yafeng Li,
 * Nagaraj C. Shivaramaiah, Dennis M. Akos and the SoftGNSS contributors.
 * DLL coefficients: Darius Plausinaitis and Dennis M. Akos.
 * See ../docs/tracking.md for conventions, quality gates and source details.
 */
#include "bds5g/b1c_tracking.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define CODE_LENGTH 10230.0
#define HALF_LENGTH 20460

struct moments { unsigned count; double mean, m2, pld_numerator; };
struct bds_b1c_tracker {
  bds_b1c_tracking_options_t options;
  int8_t data[HALF_LENGTH + 2], pilot[HALF_LENGTH + 2];
  int prn, initialized, discontinuous;
  uint64_t generation, epoch, next_sample, epoch_first;
  size_t count, required;
  double carrier_basis, code_basis, carrier, code_rate;
  double code_phase, carrier_phase, code_step;
  double osc_i, osc_q, rotate_i, rotate_q;
  long double carrier_cycles;
  double tau1, tau2, pf1, pf2, pf3;
  double old_code_nco, old_code_error, d_carrier, d2_carrier;
  bds_b1c_correlation_t de, dp, dl, pe, pp, pl;
  bds_b1c_correlation_t data_half[2], pilot_half[2];
  struct moments data_moments, pilot_moments;
  double quality_dll_sum, quality_duration_sum, quality_frequency_sum;
  double data_cn0, pilot_cn0, cn0, data_pld, pilot_pld, pld;
  unsigned good_windows, bad_windows;
  uint8_t quality_good;
  bds_b1c_lock_state_t state;
};

void bds_b1c_tracking_defaults(bds_b1c_tracking_options_t *o)
{
  if (!o) return;
  *o = (bds_b1c_tracking_options_t){ .sample_rate_hz=4092000, .if_hz=0,
    .carrier_rf_hz=1575420000, .dll_bandwidth_hz=1, .dll_damping=.7,
    .early_late_offset_chips=.06, .pll_bandwidth_hz=18, .quality_epochs=50,
    .min_cn0_db_hz=25, .min_lock_indicator=.6, .max_residual_frequency_hz=25,
    .good_windows_to_lock=2, .bad_windows_to_lose=2 };
}

static int valid_options(const bds_b1c_tracking_options_t *o)
{
  return o && isfinite(o->sample_rate_hz) && o->sample_rate_hz >= 2046000 &&
    o->sample_rate_hz <= 1e8 && isfinite(o->if_hz) &&
    fabs(o->if_hz) < o->sample_rate_hz / 2 &&
    isfinite(o->carrier_rf_hz) && o->carrier_rf_hz >= 1e6 && o->carrier_rf_hz <= 1e11 &&
    isfinite(o->dll_bandwidth_hz) && o->dll_bandwidth_hz > 0 && o->dll_bandwidth_hz <= 20 &&
    isfinite(o->dll_damping) && o->dll_damping > 0 && o->dll_damping <= 5 &&
    isfinite(o->early_late_offset_chips) && o->early_late_offset_chips > 0 && o->early_late_offset_chips < .5 &&
    isfinite(o->pll_bandwidth_hz) && o->pll_bandwidth_hz > 0 && o->pll_bandwidth_hz <= 18 &&
    o->quality_epochs >= 4 && o->quality_epochs <= 1000 &&
    isfinite(o->min_cn0_db_hz) && o->min_cn0_db_hz >= 0 && o->min_cn0_db_hz <= 80 &&
    isfinite(o->min_lock_indicator) && o->min_lock_indicator > 0 && o->min_lock_indicator <= 1 &&
    isfinite(o->max_residual_frequency_hz) && o->max_residual_frequency_hz > 0 &&
    o->max_residual_frequency_hz < 100 &&
    o->good_windows_to_lock > 0 && o->good_windows_to_lock <= 100 &&
    o->bad_windows_to_lose > 0 && o->bad_windows_to_lose <= 100;
}

static int begin_epoch(bds_b1c_tracker_t *t)
{
  t->code_step = t->code_rate / t->options.sample_rate_hz;
  const double required = ceil((CODE_LENGTH - t->code_phase) / t->code_step);
  if (!isfinite(required) || required < .005 * t->options.sample_rate_hz ||
      required > .02 * t->options.sample_rate_hz) return -2;
  t->required = (size_t)required; t->count = 0; t->epoch_first = t->next_sample;
  t->de = t->dp = t->dl = t->pe = t->pp = t->pl = (bds_b1c_correlation_t){0,0};
  memset(t->data_half,0,sizeof(t->data_half)); memset(t->pilot_half,0,sizeof(t->pilot_half));
  t->osc_i = cos(t->carrier_phase); t->osc_q = sin(t->carrier_phase);
  const double angle = 2 * PI * t->carrier / t->options.sample_rate_hz;
  t->rotate_i = cos(angle); t->rotate_q = sin(angle);
  return 0;
}

int bds_b1c_tracker_reset(bds_b1c_tracker_t *t,
                          const bds_b1c_acquisition_t *seed, uint64_t window_start)
{
  if (!t || !valid_options(&t->options) || !seed || !seed->acquired ||
      seed->prn < 1 || seed->prn > 63 || !isfinite(seed->doppler_hz) ||
      seed->code_phase_samples >= (size_t)llround(t->options.sample_rate_hz * .01) ||
      window_start > UINT64_MAX - seed->code_phase_samples ||
      fabs(t->options.if_hz + seed->doppler_hz) >= t->options.sample_rate_hz / 2 ||
      (t->initialized && t->generation == UINT64_MAX)) return -1;
  bds_b1c_tracker_t fresh = {0};
  fresh.options = t->options;
  fresh.prn = seed->prn; fresh.next_sample = window_start + seed->code_phase_samples;
  fresh.carrier_basis = fresh.carrier = fresh.options.if_hz + seed->doppler_hz;
  fresh.code_basis = fresh.code_rate = BDS_B1C_CHIP_RATE_HZ +
                     (seed->doppler_hz / fresh.options.carrier_rf_hz) * BDS_B1C_CHIP_RATE_HZ;
  if (fresh.code_rate < .99 * BDS_B1C_CHIP_RATE_HZ || fresh.code_rate > 1.01 * BDS_B1C_CHIP_RATE_HZ) return -1;
  int8_t chips[10230];
  for (int component = 0; component < 2; ++component) {
    int8_t *wave = component ? fresh.pilot : fresh.data;
    if (bds_b1c_generate_code(seed->prn, (bds_b1c_component_t)component, chips, sizeof(chips))) return -1;
    for (int k = 0; k < 10230; ++k) { wave[2*k+1] = -chips[k]; wave[2*k+2] = chips[k]; }
    wave[0] = wave[HALF_LENGTH]; wave[HALF_LENGTH+1] = wave[1];
  }
  double wn = fresh.options.dll_bandwidth_hz * 8 * fresh.options.dll_damping /
              (4 * fresh.options.dll_damping * fresh.options.dll_damping + 1);
  fresh.tau1 = 1 / (wn * wn); fresh.tau2 = 2 * fresh.options.dll_damping / wn;
  wn = fresh.options.pll_bandwidth_hz / .7845;
  fresh.pf3 = wn * wn * wn * .01 * .01;
  fresh.pf2 = 1.1 * wn * wn * .01; fresh.pf1 = 2.4 * wn;
  fresh.data_cn0 = fresh.pilot_cn0 = fresh.cn0 = NAN;
  fresh.data_pld = fresh.pilot_pld = fresh.pld = NAN;
  fresh.state = BDS_B1C_PULL_IN;
  fresh.generation = t->initialized ? t->generation + 1U : 0;
  fresh.initialized = 1;
  if (begin_epoch(&fresh)) return -1;
  *t = fresh;
  return 0;
}

int bds_b1c_tracker_create(const bds_b1c_tracking_options_t *o,
                            const bds_b1c_acquisition_t *seed, uint64_t window_start,
                            bds_b1c_tracker_t **out)
{
  if (!out || !valid_options(o)) return -1;
  *out = NULL;
  bds_b1c_tracker_t *t = calloc(1, sizeof(*t));
  if (!t) return -2;
  t->options = *o;
  if (bds_b1c_tracker_reset(t, seed, window_start)) { free(t); return -1; }
  *out = t; return 0;
}
void bds_b1c_tracker_destroy(bds_b1c_tracker_t *t) { free(t); }
uint64_t bds_b1c_tracker_next_sample(const bds_b1c_tracker_t *t) { return t ? t->next_sample : 0; }
uint64_t bds_b1c_tracker_epoch_count(const bds_b1c_tracker_t *t) { return t ? t->epoch : 0; }
size_t bds_b1c_tracker_pending_samples(const bds_b1c_tracker_t *t) { return t ? t->count : 0; }
size_t bds_b1c_tracker_samples_to_epoch(const bds_b1c_tracker_t *t) { return t ? t->required-t->count : 0; }
bds_b1c_lock_state_t bds_b1c_tracker_state(const bds_b1c_tracker_t *t) { return t ? t->state : BDS_B1C_LOST; }

static double power(bds_b1c_correlation_t z) { return z.i*z.i + z.q*z.q; }
static double costas(double i, double q)
{
  double angle = atan2(q, i);
  if (angle > PI/2) angle -= PI;
  if (angle < -PI/2) angle += PI;
  return angle / (2 * PI);
}
static double dll(bds_b1c_correlation_t e, bds_b1c_correlation_t l)
{
  const double a = sqrt(power(e)), b = sqrt(power(l));
  return a + b > 0 ? (a-b)/(a+b) : 0;
}
static void moment_add(struct moments *m, double i, double q)
{
  const double z = i*i + q*q;
  ++m->count;
  double delta = z - m->mean;
  m->mean += delta / m->count; m->m2 += delta * (z - m->mean);
  m->pld_numerator += i*i - q*q;
}
static double cn0_estimate(const struct moments *m, double duration, double *signal)
{
  *signal = 0;
  if (m->count < 2 || m->mean <= 0 || duration <= 0) return NAN;
  double discriminant = m->mean*m->mean - m->m2/(m->count-1U);
  if (discriminant <= 0) return NAN;
  *signal = sqrt(discriminant);
  double noise = m->mean - *signal;
  if (noise <= DBL_EPSILON * m->mean) return INFINITY;
  return 10 * log10(*signal / (noise * duration));
}
static void update_quality(bds_b1c_tracker_t *t)
{
  double sd, sp;
  const double dt = t->quality_duration_sum / t->options.quality_epochs;
  t->data_cn0 = cn0_estimate(&t->data_moments, dt, &sd);
  t->pilot_cn0 = cn0_estimate(&t->pilot_moments, dt, &sp);
  double total_ratio = 0;
  if (!isnan(t->data_cn0)) total_ratio += pow(10, t->data_cn0 / 10);
  if (!isnan(t->pilot_cn0)) total_ratio += pow(10, t->pilot_cn0 / 10);
  t->cn0 = total_ratio > 0 ? 10 * log10(total_ratio) : NAN;
  t->data_pld = t->data_moments.mean > 0 ? t->data_moments.pld_numerator /
               (t->data_moments.count*t->data_moments.mean) : NAN;
  t->pilot_pld = t->pilot_moments.mean > 0 ? t->pilot_moments.pld_numerator /
               (t->pilot_moments.count*t->pilot_moments.mean) : NAN;
  t->pld = sd+sp > 0 ? ((sd > 0 ? sd*t->data_pld : 0) +
                        (sp > 0 ? sp*t->pilot_pld : 0))/(sd+sp) : NAN;
  t->quality_good = !isnan(t->cn0) && t->cn0 >= t->options.min_cn0_db_hz &&
    isfinite(t->pld) && t->pld >= t->options.min_lock_indicator &&
    t->quality_dll_sum/t->options.quality_epochs < .25 &&
    t->quality_frequency_sum/t->options.quality_epochs < t->options.max_residual_frequency_hz;
  if (t->quality_good) {
    t->bad_windows = 0;
    if (t->good_windows < t->options.good_windows_to_lock) ++t->good_windows;
    if (t->good_windows >= t->options.good_windows_to_lock && t->state!=BDS_B1C_LOST)
      t->state = BDS_B1C_LOCKED;
  } else {
    t->good_windows = 0;
    if (t->bad_windows < t->options.bad_windows_to_lose) ++t->bad_windows;
    if (t->bad_windows >= t->options.bad_windows_to_lose) t->state = BDS_B1C_LOST;
  }
  t->data_moments = t->pilot_moments = (struct moments){0};
  t->quality_dll_sum = t->quality_duration_sum = t->quality_frequency_sum = 0;
}

static int finish_epoch(bds_b1c_tracker_t *t, bds_b1c_tracking_callback_t callback, void *context)
{
  bds_b1c_tracking_epoch_t r = {0};
  r.prn=t->prn; r.epoch=t->epoch; r.generation=t->generation;
  r.absolute_sample=t->epoch_first; r.sample_count=t->count;
  r.sample_time_s=(double)t->epoch_first/t->options.sample_rate_hz;
  r.integration_s=(double)t->count/t->options.sample_rate_hz;
  r.carrier_hz=t->carrier; r.doppler_hz=t->carrier-t->options.if_hz;
  r.code_rate_hz=t->code_rate; r.rem_code_phase=t->code_phase;
  r.rem_carrier_phase=t->carrier_phase; r.carrier_cycles=(double)t->carrier_cycles;
  r.data_early=t->de; r.data_prompt=t->dp; r.data_late=t->dl;
  r.pilot_early=t->pe; r.pilot_prompt=t->pp; r.pilot_late=t->pl;
  r.pll_error=(11*costas(t->dp.i,t->dp.q) + 29*costas(t->pp.q,-t->pp.i))/40;
  r.dll_error=(11*dll(t->de,t->dl) + 29*dll(t->pe,t->pl))/40 *
              (1-t->options.early_late_offset_chips);
  double cross_i=0,cross_q=0;
  for (int component=0;component<2;++component) {
    bds_b1c_correlation_t *h=component ? t->pilot_half : t->data_half;
    cross_i+=h[0].i*h[1].i+h[0].q*h[1].q;
    cross_q+=h[0].i*h[1].q-h[0].q*h[1].i;
  }
  r.residual_frequency_hz=cross_i!=0 || cross_q!=0 ?
    atan2(cross_q,cross_i)/(PI*r.integration_s) : NAN;
  if (power(t->dp)+power(t->pp) > 0) {
    t->d2_carrier += r.pll_error*t->pf3;
    t->d_carrier += t->d2_carrier + r.pll_error*t->pf2;
    r.carrier_nco_hz = t->d_carrier + r.pll_error*t->pf1;
    r.code_nco_hz = t->old_code_nco + (t->tau2/t->tau1)*(r.dll_error-t->old_code_error) +
                    r.dll_error*(.01/t->tau1);
    t->old_code_error=r.dll_error; t->old_code_nco=r.code_nco_hz;
  } else {
    r.carrier_nco_hz=t->carrier-t->carrier_basis;
    r.code_nco_hz=t->old_code_nco;
  }
  moment_add(&t->data_moments,t->dp.i,t->dp.q);
  moment_add(&t->pilot_moments,t->pp.q,-t->pp.i);
  t->quality_dll_sum += fabs(r.dll_error); t->quality_duration_sum += r.integration_s;
  t->quality_frequency_sum += isfinite(r.residual_frequency_hz) ? fabs(r.residual_frequency_hz) : 100;
  if (t->data_moments.count == t->options.quality_epochs) {
    update_quality(t); r.quality_updated=1;
  }
  r.data_cn0_db_hz=t->data_cn0; r.pilot_cn0_db_hz=t->pilot_cn0; r.cn0_db_hz=t->cn0;
  r.data_lock_indicator=t->data_pld; r.pilot_lock_indicator=t->pilot_pld; r.lock_indicator=t->pld;
  r.quality_good=t->quality_good; r.state=t->state;
  r.valid=t->state==BDS_B1C_LOCKED && t->quality_good && power(t->dp)+power(t->pp)>0 &&
          isfinite(r.residual_frequency_hz) &&
          fabs(r.residual_frequency_hz)<1.6*t->options.max_residual_frequency_hz;
  /* Retain sample/code/carrier continuity even when the quality flag is bad. */
  t->code_phase=(t->code_phase+(t->count-1U)*t->code_step)+t->code_step-CODE_LENGTH;
  t->carrier_phase=fmod(t->carrier_phase+(t->carrier*2*PI)*r.integration_s,2*PI);
  t->carrier_cycles+=(long double)t->carrier*(long double)t->count/t->options.sample_rate_hz;
  t->carrier=t->carrier_basis+r.carrier_nco_hz;
  t->code_rate=t->code_basis-r.code_nco_hz;
  if (!isfinite(t->carrier) || fabs(t->carrier)>=t->options.sample_rate_hz/2 ||
      !isfinite(t->code_rate) || t->code_rate<.99*BDS_B1C_CHIP_RATE_HZ ||
      t->code_rate>1.01*BDS_B1C_CHIP_RATE_HZ) {
    t->state=BDS_B1C_LOST; t->discontinuous=1; r.state=BDS_B1C_LOST; r.valid=0;
    if (callback) callback(&r,context);
    ++t->epoch; return -2;
  }
  ++t->epoch;
  if (callback) callback(&r,context);
  return begin_epoch(t);
}

int bds_b1c_tracker_push(bds_b1c_tracker_t *t, const bds5g_cf32_t *samples,
                         size_t count, uint64_t first_sample,
                         bds_b1c_tracking_callback_t callback, void *context)
{
  if (!t || !t->initialized || (!samples && count) || count>UINT64_MAX-first_sample) return -1;
  if (t->discontinuous || first_sample!=t->next_sample) {
    t->state=BDS_B1C_LOST; t->quality_good=0; t->discontinuous=1; return -3;
  }
  for (size_t k=0;k<count;++k) if (!isfinite(samples[k].i)||!isfinite(samples[k].q)) return -1;
  const double spacing=t->options.early_late_offset_chips;
  for (size_t k=0;k<count;++k) {
    const double phase=t->code_phase+t->count*t->code_step;
    const int e=(int)ceil((phase-spacing)*2), p=(int)ceil(phase*2), l=(int)ceil((phase+spacing)*2);
    if (e<0 || l>HALF_LENGTH+1 || p<0 || p>HALF_LENGTH+1) {
      t->state=BDS_B1C_LOST; t->discontinuous=1; return -2;
    }
    const double i=samples[k].i*t->osc_i+samples[k].q*t->osc_q;
    const double q=samples[k].q*t->osc_i-samples[k].i*t->osc_q;
    t->de.i+=i*t->data[e]; t->de.q+=q*t->data[e];
    t->dp.i+=i*t->data[p]; t->dp.q+=q*t->data[p];
    t->dl.i+=i*t->data[l]; t->dl.q+=q*t->data[l];
    t->pe.i+=i*t->pilot[e]; t->pe.q+=q*t->pilot[e];
    t->pp.i+=i*t->pilot[p]; t->pp.q+=q*t->pilot[p];
    t->pl.i+=i*t->pilot[l]; t->pl.q+=q*t->pilot[l];
    unsigned half=t->count<t->required/2 ? 0U : 1U;
    t->data_half[half].i+=i*t->data[p]; t->data_half[half].q+=q*t->data[p];
    t->pilot_half[half].i+=i*t->pilot[p]; t->pilot_half[half].q+=q*t->pilot[p];
    double next_i=t->osc_i*t->rotate_i-t->osc_q*t->rotate_q;
    t->osc_q=t->osc_q*t->rotate_i+t->osc_i*t->rotate_q; t->osc_i=next_i;
    ++t->count; ++t->next_sample;
    if ((t->count&1023U)==0) {
      double magnitude=sqrt(t->osc_i*t->osc_i+t->osc_q*t->osc_q);
      t->osc_i/=magnitude; t->osc_q/=magnitude;
    }
    if (t->count==t->required) {
      int error=finish_epoch(t,callback,context); if (error) return error;
    }
  }
  return 0;
}

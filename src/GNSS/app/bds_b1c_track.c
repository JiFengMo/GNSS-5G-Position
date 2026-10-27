#include "bds5g/b1c_tracking.h"
#include "bds5g/b1c_parallel.h"
#include "b1c_file_io.h"

struct pending_event {
  uint64_t generation,sample; const char *name; double metric,carrier; size_t phase;
};
struct channel {
  bds_b1c_tracker_t *tracker;
  bds_b1c_acquisition_t seed;
  uint64_t window_start, target, written, first, last_end, locked, valid, lost;
  unsigned attempts, successes;
  uint64_t next_attempt;
  double last_doppler;
  int active, error, io_error, last_state;
  FILE *csv, *events;
  struct pending_event *pending_events;
  size_t event_count,event_capacity;
  int defer_events;
  b1c_file_reader_t reacquisition_reader;
  bds5g_cf32_t *acquisition_samples;
};
static const char *state_name(int state)
{ return state==BDS_B1C_LOCKED ? "locked" : state==BDS_B1C_LOST ? "lost" : "pull_in"; }
static void event(struct channel *c, uint64_t generation, uint64_t sample,
                    const char *name, double metric, double carrier, size_t phase)
{
  if (c->defer_events) {
    if (c->event_count==c->event_capacity) {
      size_t capacity=c->event_capacity?2*c->event_capacity:8;
      void *pending=realloc(c->pending_events,capacity*sizeof(*c->pending_events));
      if (!pending) {c->io_error=1;return;}
      c->pending_events=pending;c->event_capacity=capacity;
    }
    c->pending_events[c->event_count++]=(struct pending_event){generation,sample,name,metric,carrier,phase};
    return;
  }
  if (fprintf(c->events,"%d,%llu,%llu,%s,%.17g,%.17g,%zu\n",c->seed.prn,
      (unsigned long long)generation,(unsigned long long)sample,name,metric,carrier,phase)<0) c->io_error=1;
}
static void epoch_record(const bds_b1c_tracking_epoch_t *r, void *context)
{
  struct channel *c=context;
  if (c->written>=c->target) return;
  if (!c->written) c->first=r->absolute_sample;
  c->last_end=r->absolute_sample+r->sample_count;
  c->last_doppler=r->doppler_hz;
  c->locked+=r->state==BDS_B1C_LOCKED; c->valid+=r->valid; c->lost+=r->state==BDS_B1C_LOST;
  if ((int)r->state!=c->last_state) {
    event(c,r->generation,c->last_end,state_name(r->state),NAN,r->carrier_hz,0);
    c->last_state=r->state;
  }
  if (fprintf(c->csv,
      "%llu,%llu,%llu,%llu,%zu,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
      "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
      "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%u,%u,%s,%u\n",
      (unsigned long long)r->epoch,(unsigned long long)r->generation,
      (unsigned long long)c->written,(unsigned long long)r->absolute_sample,r->sample_count,
      r->sample_time_s,r->integration_s,r->carrier_hz,r->doppler_hz,r->code_rate_hz,
      r->rem_code_phase,r->rem_carrier_phase,r->carrier_cycles,r->dll_error,r->pll_error,
      r->code_nco_hz,r->data_early.i,r->data_early.q,r->data_prompt.i,r->data_prompt.q,
      r->data_late.i,r->data_late.q,r->pilot_prompt.i,r->pilot_prompt.q,
      r->carrier_nco_hz,r->pilot_early.i,r->pilot_early.q,r->pilot_late.i,
      r->pilot_late.q,r->data_cn0_db_hz,r->pilot_cn0_db_hz,r->cn0_db_hz,
      r->data_lock_indicator,r->pilot_lock_indicator,r->lock_indicator,r->residual_frequency_hz,
      (unsigned)r->quality_updated,(unsigned)r->quality_good,state_name(r->state),(unsigned)r->valid)<0) c->io_error=1;
  ++c->written;
}
static void header(FILE *f)
{
  fputs("epoch,generation,record_index,absolute_sample,sample_count,sample_time_s,integration_s,carrier_hz,doppler_hz,"
        "code_rate_hz,rem_code_phase,rem_carrier_phase,carrier_cycles,dll_error,pll_error,code_nco_hz,"
        "data_i_e,data_q_e,data_i_p,data_q_p,data_i_l,data_q_l,pilot_i_p,pilot_q_p,carrier_nco_hz,"
        "pilot_i_e,pilot_q_e,pilot_i_l,pilot_q_l,data_cn0_db_hz,pilot_cn0_db_hz,cn0_db_hz,"
        "data_lock_indicator,pilot_lock_indicator,lock_indicator,residual_frequency_hz,quality_updated,quality_good,state,valid\n",f);
}
static int close_output(FILE *f)
{
  int status=ferror(f); status|=fclose(f)!=0; return status;
}
static void json_string(FILE *f,const char *s)
{
  fputc('"',f);
  for (;*s;++s) {
    unsigned char ch=(unsigned char)*s;
    if (ch=='"'||ch=='\\') { fputc('\\',f); fputc(ch,f); }
    else if (ch<32) fprintf(f,"\\u%04x",ch);
    else fputc(ch,f);
  }
  fputc('"',f);
}
static void usage(const char *p)
{
  fprintf(stderr,"Usage: %s --input IQ --acquisition-csv C_CAPTURE_CSV --sample-rate HZ --output-dir DIR [options]\n"
    "  --input-format ci8|cf32    Default cf32 (little-endian), I+jQ\n"
    "  --if HZ                   Default 0, must match acquisition\n"
    "  --seconds S               Default 60, multiple of .01, up to 3600\n"
    "  --prns LIST               Optional comma-separated acquired PRNs\n"
    "  --threads N               Channel workers, 1..63; default 1 (serial)\n"
    "  --chunk-samples N         Default 65536, independent of code epochs\n"
    "  --dll-bandwidth HZ        Default 1\n"
    "  --pll-bandwidth HZ        Default 18 (0 < BW <=18)\n"
    "  --early-late CHIPS        Half E-L spacing, default .06\n"
    "  --quality-epochs N        Default 50\n"
    "  --min-cn0 DBHZ            Default 25\n"
    "  --min-lock VALUE          Default .6\n"
    "  --max-residual-frequency HZ Default 25, half-epoch false-lock guard\n"
    "  --max-reacquisitions N    Default 3 per channel, 1 s minimum spacing\n"
    "  --no-reacquire            Keep continuous loops for MATLAB comparison\n"
    "Outputs: per-PRN epoch CSVs, events.csv, summary.csv. Offline tracking only.\n",p);
}
static int selected(const char *list, int prn)
{
  if (!list) return 1;
  while (*list) {
    char *end; long value=strtol(list,&end,10);
    if (end==list) return 0;
    if (value==prn) return 1;
    list=*end ? end+1 : end;
  }
  return 0;
}
static FILE *output_file(const char *directory, const char *name, const char *iq, const char *capture)
{
  char path[4096];
  if (snprintf(path,sizeof(path),"%s/%s",directory,name)>=(int)sizeof(path) ||
      b1c_same_file(path,iq) || b1c_same_file(path,capture)) return NULL;
  return fopen(path,"w");
}
struct tracking_batch {
  struct channel *channels;
  const bds5g_cf32_t *samples;
  uint64_t cursor,end;
  const bds_b1c_tracking_options_t *options;
  unsigned max_reacquisitions;
  size_t acquisition_count;
  const char *input;
  int ci8;
};
static void track_channel(size_t index, void *context)
{
  const struct tracking_batch *batch=context;
  struct channel *c=&batch->channels[index];
  const bds5g_cf32_t *samples=batch->samples;
  const bds_b1c_tracking_options_t *options=batch->options;
  uint64_t cursor=batch->cursor,end=batch->end;
  unsigned max_reacquisitions=batch->max_reacquisitions;
  size_t acquisition_count=batch->acquisition_count;
  while (c->active) {
    uint64_t next=bds_b1c_tracker_next_sample(c->tracker);
    if (next>=end) break;
    if (next<cursor) { c->error=6; break; }
    size_t n=bds_b1c_tracker_samples_to_epoch(c->tracker);
    if (n>end-next) n=(size_t)(end-next);
    int result=bds_b1c_tracker_push(c->tracker,samples+(size_t)(next-cursor),n,next,epoch_record,c);
    if (result) { c->error=6; break; }
    if (c->io_error) { c->error=3; break; }
    if (c->written>=c->target) { c->active=0; break; }
    next=bds_b1c_tracker_next_sample(c->tracker);
    if (bds_b1c_tracker_state(c->tracker)==BDS_B1C_LOST &&
        !bds_b1c_tracker_pending_samples(c->tracker) && next>=c->next_attempt && c->attempts<max_reacquisitions) {
      ++c->attempts; c->next_attempt=next+(uint64_t)llround(options->sample_rate_hz);
      size_t got=0; bds_b1c_acquisition_t found={0};
      bds_b1c_acquisition_options_t ao; bds_b1c_acquisition_defaults(&ao);
      ao.sample_rate_hz=options->sample_rate_hz; ao.if_hz=options->if_hz;
      ao.doppler_min_hz=c->last_doppler-1000; ao.doppler_max_hz=c->last_doppler+1000;
      if (!c->acquisition_samples) c->acquisition_samples=malloc(acquisition_count*sizeof(*c->acquisition_samples));
      int status=!c->acquisition_samples;
      if (!status && !c->reacquisition_reader.file) status=b1c_file_open(&c->reacquisition_reader,batch->input,batch->ci8);
      if (!status) status=b1c_file_seek(&c->reacquisition_reader,next) ||
        b1c_file_read(&c->reacquisition_reader,c->acquisition_samples,acquisition_count,&got) || got!=acquisition_count;
      if (!status) status=bds_b1c_acquire_many(c->acquisition_samples,got,&ao,&c->seed.prn,1,&found);
      if (!status && found.acquired && !bds_b1c_tracker_reset(c->tracker,&found,next)) {
        ++c->successes; c->last_state=BDS_B1C_PULL_IN;
        event(c,c->successes,bds_b1c_tracker_next_sample(c->tracker),"reacquire_ok",found.peak_metric,found.carrier_hz,found.code_phase_samples);
      } else event(c,c->successes,next,"reacquire_failed",found.peak_metric,NAN,0);
    }
  }
}

int main(int argc, char **argv)
{
  const char *input=NULL, *capture=NULL, *directory=NULL, *prn_filter=NULL;
  unsigned threads=1;
  int ci8=0; double seconds=60; size_t chunk=65536; unsigned max_reacquisitions=3;
  bds_b1c_tracking_options_t options; bds_b1c_tracking_defaults(&options); options.sample_rate_hz=0;
  for (int k=1;k<argc;++k) {
    const char *key=argv[k];
    if (!strcmp(key,"--help")) { usage(argv[0]); return 0; }
    if (!strcmp(key,"--no-reacquire")) { max_reacquisitions=0; continue; }
    if (k+1>=argc) goto invalid;
    const char *value=argv[++k]; double *number=NULL, integer=0;
    if (!strcmp(key,"--input")) input=value;
    else if (!strcmp(key,"--acquisition-csv")) capture=value;
    else if (!strcmp(key,"--output-dir")) directory=value;
    else if (!strcmp(key,"--prns")) {
      prn_filter=value; const char *p=value;
      if (!*p) goto invalid;
      while (*p) { char *end; if (*p<'0'||*p>'9') goto invalid;
        long n=strtol(p,&end,10); if (n<1||n>63) goto invalid;
        p=end; if (*p && (*p++!=',' || !*p)) goto invalid; }
    } else if (!strcmp(key,"--threads")) {
      if (b1c_number(value,&integer) || integer<1 || integer>63 || integer!=floor(integer)) goto invalid;
      threads=(unsigned)integer;
    } else if (!strcmp(key,"--input-format")) {
      if (strcmp(value,"ci8") && strcmp(value,"cf32")) goto invalid;
      ci8=!strcmp(value,"ci8");
    } else if (!strcmp(key,"--sample-rate")) number=&options.sample_rate_hz;
    else if (!strcmp(key,"--if")) number=&options.if_hz;
    else if (!strcmp(key,"--seconds")) number=&seconds;
    else if (!strcmp(key,"--dll-bandwidth")) number=&options.dll_bandwidth_hz;
    else if (!strcmp(key,"--pll-bandwidth")) number=&options.pll_bandwidth_hz;
    else if (!strcmp(key,"--early-late")) number=&options.early_late_offset_chips;
    else if (!strcmp(key,"--min-cn0")) number=&options.min_cn0_db_hz;
    else if (!strcmp(key,"--min-lock")) number=&options.min_lock_indicator;
    else if (!strcmp(key,"--max-residual-frequency")) number=&options.max_residual_frequency_hz;
    else if (!strcmp(key,"--quality-epochs") || !strcmp(key,"--max-reacquisitions") || !strcmp(key,"--chunk-samples")) {
      if (b1c_number(value,&integer) || integer<0 || integer!=floor(integer) || integer>1000000) goto invalid;
      if (!strcmp(key,"--quality-epochs")) options.quality_epochs=(unsigned)integer;
      else if (!strcmp(key,"--max-reacquisitions")) { if (integer>100) goto invalid; max_reacquisitions=(unsigned)integer; }
      else { if (integer<1) goto invalid; chunk=(size_t)integer; }
    } else goto invalid;
    if (number && b1c_number(value,number)) goto invalid;
  }
  if (!input||!capture||!directory || options.sample_rate_hz<2046000 || options.sample_rate_hz>1e8 ||
      seconds<.01 || seconds>3600 || fabs(seconds*100-round(seconds*100))>1e-6) goto invalid;
  struct channel channels[63]={0}; size_t channel_count=0;
  FILE *seeds=fopen(capture,"r"); if (!seeds) { perror("acquisition CSV"); return 3; }
  char line[4096]; int error=0;
  if (!fgets(line,sizeof(line),seeds) || strncmp(line,"prn,carrier_hz,doppler_hz,code_phase_samples,",43)) { fclose(seeds); goto invalid; }
  while (fgets(line,sizeof(line),seeds)) {
    int prn; double carrier,doppler,metric,cn0,coarse,fine; unsigned acquired;
    unsigned long long phase,start;
    char tail;
    if (sscanf(line,"%d,%lf,%lf,%llu,%lf,%lf,%u,%lf,%lf,%llu %c",&prn,&carrier,&doppler,&phase,
        &metric,&cn0,&acquired,&coarse,&fine,&start,&tail)!=10 || prn<1 || prn>63 || acquired>1) { error=2; break; }
    if (!acquired || !selected(prn_filter,prn)) continue;
    if (channel_count==63 || !isfinite(carrier)||!isfinite(doppler)||!isfinite(metric)||
        fabs(carrier-options.if_hz-doppler)>1e-5 || phase>SIZE_MAX) { error=2; break; }
    for (size_t j=0;j<channel_count;++j) if (channels[j].seed.prn==prn) error=2;
    if (error) break;
    struct channel *c=&channels[channel_count++];
    c->seed=(bds_b1c_acquisition_t){.prn=prn,.carrier_hz=carrier,.doppler_hz=doppler,
      .code_phase_samples=(size_t)phase,.peak_metric=metric,.acquired=1};
    c->window_start=(uint64_t)start; c->target=(uint64_t)llround(seconds*100);
    c->last_doppler=doppler; c->last_state=BDS_B1C_PULL_IN;
    if (bds_b1c_tracker_create(&options,&c->seed,c->window_start,&c->tracker)) { error=2; break; }
    c->active=1;
  }
  if (ferror(seeds)) error=3;
  fclose(seeds);
  if (!error && !channel_count) { fprintf(stderr,"No acquired PRNs selected\n"); return 7; }
  if (!error && b1c_mkdirs(directory)) error=3;
  FILE *events=NULL, *summary=NULL;
  b1c_file_reader_t reader={0};
  bds5g_cf32_t *samples=NULL;
  bds_b1c_worker_pool_t *pool=NULL;
  if (!error) {
    events=output_file(directory,"events.csv",input,capture);
    summary=output_file(directory,"summary.csv",input,capture);
    if (!events||!summary) error=3;
  }
  uint64_t cursor=UINT64_MAX;
  for (size_t j=0;j<channel_count && !error;++j) {
    struct channel *c=&channels[j]; char name[64];
    snprintf(name,sizeof(name),"prn_%02d.csv",c->seed.prn);
    c->csv=output_file(directory,name,input,capture); c->events=events;
    if (!c->csv) { error=3; break; }
    header(c->csv);
    uint64_t next=bds_b1c_tracker_next_sample(c->tracker); if (next<cursor) cursor=next;
  }
  size_t acquisition_count=2U*(size_t)llround(options.sample_rate_hz*.01);
  if (!error) {
    fputs("prn,generation,absolute_sample,event,peak_metric,carrier_hz,code_phase_samples\n",events);
    for (size_t j=0;j<channel_count;++j)
      event(&channels[j],0,bds_b1c_tracker_next_sample(channels[j].tracker),"init",channels[j].seed.peak_metric,
            channels[j].seed.carrier_hz,channels[j].seed.code_phase_samples);
    samples=malloc(chunk*sizeof(*samples));
    if (!samples || b1c_file_open(&reader,input,ci8) || b1c_file_seek(&reader,cursor) ||
        bds_b1c_worker_pool_create(threads<channel_count?threads:(unsigned)channel_count,&pool)) error=3;
    for (size_t j=0;j<channel_count;++j) channels[j].defer_events=threads>1;
  }
  fprintf(stderr,"B1C continuous tracking: %zu channels, %.2f s, fs=%.0f, chunk=%zu\n",channel_count,seconds,options.sample_rate_hz,chunk);
  while (!error) {
    size_t active=0;
    for (size_t j=0;j<channel_count;++j) active+=channels[j].active!=0;
    if (!active) break;
    size_t count=0;
    if (b1c_file_read(&reader,samples,chunk,&count)) { error=3; break; }
    if (!count) { error=3; fprintf(stderr,"EOF before all requested full code epochs completed\n"); break; }
    if (cursor>UINT64_MAX-count) { error=6; break; }
    uint64_t end=cursor+count;
    struct tracking_batch batch={channels,samples,cursor,end,&options,max_reacquisitions,acquisition_count,input,ci8};
    if (bds_b1c_worker_pool_run(pool,channel_count,track_channel,&batch)) error=6;
    /* Replay shared events in the original channel order after the barrier.
     * Per-PRN CSV files and reacquisition readers have no shared writer/state. */
    for (size_t j=0;j<channel_count;++j) {
      struct channel *c=&channels[j];c->defer_events=0;
      for (size_t k=0;k<c->event_count;++k) {
        const struct pending_event *e=&c->pending_events[k];
        event(c,e->generation,e->sample,e->name,e->metric,e->carrier,e->phase);
      }
      c->event_count=0;c->defer_events=threads>1;
      if (!error && (c->error||c->io_error)) error=c->error?c->error:3;
    }
    cursor=end;
  }
  if (summary) {
    fputs("prn,epochs,first_sample,last_sample_end,locked_epochs,valid_epochs,lost_epochs,reacquisition_attempts,reacquisition_successes,pending_samples,complete,final_state\n",summary);
    for (size_t j=0;j<channel_count;++j) {
      struct channel *c=&channels[j];
      fprintf(summary,"%d,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%zu,%d,%s\n",c->seed.prn,
        (unsigned long long)c->written,(unsigned long long)c->first,(unsigned long long)c->last_end,
        (unsigned long long)c->locked,(unsigned long long)c->valid,(unsigned long long)c->lost,
        c->attempts,c->successes,bds_b1c_tracker_pending_samples(c->tracker),
        c->written==c->target&&!c->error,state_name(bds_b1c_tracker_state(c->tracker)));
      fprintf(stderr,"PRN %02d: %llu epochs, %llu valid, %llu lost, reacquisitions=%u/%u\n",c->seed.prn,
        (unsigned long long)c->written,(unsigned long long)c->valid,(unsigned long long)c->lost,c->successes,c->attempts);
    }
    if (close_output(summary)) error=3;
  }
  if (events && close_output(events)) error=3;
  for (size_t j=0;j<channel_count;++j) {
    if (channels[j].csv && close_output(channels[j].csv)) error=3;
    bds_b1c_tracker_destroy(channels[j].tracker);
    b1c_file_close(&channels[j].reacquisition_reader);
    free(channels[j].acquisition_samples);free(channels[j].pending_events);
  }
  bds_b1c_worker_pool_destroy(pool);
  b1c_file_close(&reader);free(samples);
  if (summary) {
    FILE *metadata=output_file(directory,"run.json",input,capture);
    if (!metadata) return 3;
    fputs("{\n  \"schema_version\": 1,\n  \"input_path\": ",metadata); json_string(metadata,input);
    fputs(",\n  \"acquisition_csv\": ",metadata); json_string(metadata,capture);
    fprintf(metadata,",\n  \"threads\": %u",threads);
    fprintf(metadata,",\n  \"input_format\": \"%s\",\n  \"sample_rate_hz\": %.17g,\n  \"if_hz\": %.17g,\n"
      "  \"requested_seconds\": %.17g,\n  \"requested_epochs_per_channel\": %llu,\n"
      "  \"chunk_samples\": %zu,\n  \"dll_bandwidth_hz\": %.17g,\n  \"dll_damping\": %.17g,\n"
      "  \"early_late_offset_chips\": %.17g,\n  \"pll_bandwidth_hz\": %.17g,\n"
      "  \"quality_epochs\": %u,\n  \"min_cn0_db_hz\": %.17g,\n  \"min_lock_indicator\": %.17g,\n"
      "  \"max_residual_frequency_hz\": %.17g,\n  \"max_reacquisitions\": %u,\n  \"status_code\": %d,\n  \"complete\": %s\n}\n",
      ci8?"ci8":"cf32",options.sample_rate_hz,options.if_hz,seconds,
      (unsigned long long)llround(seconds*100),chunk,options.dll_bandwidth_hz,options.dll_damping,
      options.early_late_offset_chips,options.pll_bandwidth_hz,options.quality_epochs,
      options.min_cn0_db_hz,options.min_lock_indicator,options.max_residual_frequency_hz,
      max_reacquisitions,error,error?"false":"true");
    if (close_output(metadata)) return 3;
  }
  return error;
invalid:
  usage(argv[0]); return 2;
}

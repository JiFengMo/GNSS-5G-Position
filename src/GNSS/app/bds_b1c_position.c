#include "bds5g/b1c_position.h"
#include "b1c_csv_io.h"

struct record { bds_b1c_tracking_epoch_t epoch; uint64_t segment; };
struct anchor { bds_b1c_nav_frame_t frame; uint64_t segment; };
struct channel {
  struct record *records; size_t count,capacity;
  struct anchor *anchors; size_t anchors_count,anchors_capacity;
};
struct options { double fs,period_ms,elevation,offset_ms,max_age; int tropo; };
static int add_record(struct channel *c,const struct record *r)
{
  if (c->count==c->capacity) {
    size_t next=c->capacity?c->capacity*2:8192;
    if (next>10000000||next>SIZE_MAX/sizeof(*c->records)) return -1;
    void *v=realloc(c->records,next*sizeof(*c->records)); if (!v) return -1;
    c->records=v; c->capacity=next;
  }
  c->records[c->count++]=*r; return 0;
}
static size_t locate(const struct channel *c,uint64_t sample)
{
  size_t lo=0,hi=c->count;
  while (lo<hi) { size_t mid=lo+(hi-lo)/2;
    if (c->records[mid].epoch.absolute_sample<=sample) lo=mid+1; else hi=mid;
  }
  return lo?lo-1:SIZE_MAX;
}
static int read_tracking(struct channel *c,int prn,const char *path,double fs)
{
  const char *names[]={"epoch","generation","absolute_sample","sample_count","integration_s", "code_rate_hz",
    "rem_code_phase","data_i_p","pilot_q_p","state","valid"};
  b1c_csv_t csv; int ix[11],result=-1,status; uint64_t segment=0;
  if (csv_open(&csv,path)) return -1;
  for (int j=0;j<11;++j) if ((ix[j]=csv_index(&csv,names[j]))<0) goto done;
  while ((status=csv_next(&csv))>0) {
    struct record r={0}; bds_b1c_tracking_epoch_t *e=&r.epoch; uint64_t n,valid;
    char **v=csv.columns; e->prn=prn;
    if (csv_u64(v[ix[0]],&e->epoch)||csv_u64(v[ix[1]],&e->generation)||
        csv_u64(v[ix[2]],&e->absolute_sample)||csv_u64(v[ix[3]],&n)||n>SIZE_MAX||!n||
        n>UINT64_MAX-e->absolute_sample||csv_number(v[ix[4]],&e->integration_s)||
        csv_number(v[ix[5]],&e->code_rate_hz)||csv_number(v[ix[6]],&e->rem_code_phase)||
        csv_number(v[ix[7]],&e->data_prompt.i)||csv_number(v[ix[8]],&e->pilot_prompt.q)||
        csv_u64(v[ix[10]],&valid)||valid>1||e->epoch==UINT64_MAX||
        e->integration_s<.009||e->integration_s>.011||fabs((double)n/fs-e->integration_s)>1e-9) goto done;
    e->sample_count=(size_t)n; e->valid=(uint8_t)valid;
    if (!strcmp(v[ix[9]],"locked")) e->state=BDS_B1C_LOCKED;
    else if (!strcmp(v[ix[9]],"pull_in")) e->state=BDS_B1C_PULL_IN;
    else if (!strcmp(v[ix[9]],"lost")) e->state=BDS_B1C_LOST;
    else goto done;
    if (e->valid&&e->state!=BDS_B1C_LOCKED) goto done;
    if (c->count) {
      const bds_b1c_tracking_epoch_t *p=&c->records[c->count-1].epoch;
      if (e->absolute_sample<p->absolute_sample+p->sample_count||e->generation<p->generation||
          (e->generation==p->generation&&e->epoch<=p->epoch)) goto done;
      if (!p->valid||!e->valid||e->generation!=p->generation||e->epoch!=p->epoch+1||
          e->absolute_sample!=p->absolute_sample+p->sample_count) ++segment;
    }
    r.segment=segment;
    if (add_record(c,&r)) goto done;
  }
  if (!status&&c->count) result=0;
done:
  if (csv_close(csv.file)) result=-1;
  if (result) fprintf(stderr,"Invalid tracking data: %s\n",path);
  return result;
}
static int read_navigation(struct channel *channels,const char *directory)
{
  char path[4096]; b1c_csv_t csv; int ix[8],result=-1,status;
  const char *names[]={"prn","generation","first_epoch","absolute_sample","end_sample","valid","bits","status"};
  if (snprintf(path,sizeof(path),"%s/frames.csv",directory)>=(int)sizeof(path)||csv_open(&csv,path)) return -1;
  for (int j=0;j<8;++j) if ((ix[j]=csv_index(&csv,names[j]))<0) goto done;
  while ((status=csv_next(&csv))>0) {
    struct anchor a={0}; bds_b1c_nav_frame_t *f=&a.frame; uint64_t prn,valid;
    char **v=csv.columns;
    if (csv_u64(v[ix[0]],&prn)||prn<1||prn>63||csv_u64(v[ix[5]],&valid)||valid>1) goto done;
    if (!valid) continue;
    if (strcmp(v[ix[7]],"ok")||csv_u64(v[ix[1]],&f->generation)||csv_u64(v[ix[2]],&f->first_epoch)||
        csv_u64(v[ix[3]],&f->absolute_sample)||csv_u64(v[ix[4]],&f->end_sample)||strlen(v[ix[6]])!=878) goto done;
    for (size_t j=0;j<878;++j) {
      char b=v[ix[6]][j]; if (b!='0'&&b!='1') goto done;
      f->bits[j]=(uint8_t)(b-'0');
    }
    f->prn=(int)prn;
    if (bds_b1c_decode_navigation(f->bits,878,(int)prn,&f->ephemeris)) goto done;
    struct channel *c=&channels[prn-1]; size_t first=locate(c,f->absolute_sample);
    if (first==SIZE_MAX||c->count-first<1800) goto done;
    a.segment=c->records[first].segment;
    uint8_t symbols[1800]; int8_t secondary[1800]; int correlation=0;
    bds_b1c_generate_secondary_code((int)prn,secondary,1800);
    for (size_t j=0;j<1800;++j) {
      const struct record *r=&c->records[first+j]; const bds_b1c_tracking_epoch_t *e=&r->epoch;
      if (!e->valid||r->segment!=a.segment||e->generation!=f->generation||e->epoch!=f->first_epoch+j) goto done;
      if (!j&&e->absolute_sample!=f->absolute_sample) goto done;
      symbols[j]=e->data_prompt.i>0;
      correlation+=(e->pilot_prompt.q>0?1:-1)*secondary[j];
    }
    const bds_b1c_tracking_epoch_t *last=&c->records[first+1799].epoch;
    if (last->absolute_sample+last->sample_count!=f->end_sample||abs(correlation)!=1800) goto done;
    bds_b1c_nav_frame_t decoded;
    if (bds_b1c_decode_frame(symbols,1800,(int)prn,&decoded)||decoded.status!=BDS_B1C_NAV_OK||
        memcmp(decoded.bits,f->bits,878)) goto done;
    /* Check later anchors against the first one in this continuous segment. */
    for (size_t j=0;j<c->anchors_count;++j) {
      const struct anchor *p=&c->anchors[j];
      if (p->frame.absolute_sample>=f->absolute_sample) goto done;
      if (p->segment==a.segment) {
        double delta=((double)f->ephemeris.WN-p->frame.ephemeris.WN)*604800+
            f->ephemeris.TOW-p->frame.ephemeris.TOW;
        if (fabs(delta-(double)(f->first_epoch-p->frame.first_epoch)*.01)>1e-8) goto done;
      }
    }
    if (c->anchors_count==c->anchors_capacity) {
      size_t cap=c->anchors_capacity?c->anchors_capacity*2:8;
      if (cap>1000000||cap>SIZE_MAX/sizeof(*c->anchors)) goto done;
      void *p=realloc(c->anchors,cap*sizeof(*c->anchors)); if (!p) goto done;
      c->anchors=p; c->anchors_capacity=cap;
    }
    c->anchors[c->anchors_count++]=a;
  }
  if (!status) result=0;
done:
  if (csv_close(csv.file)) result=-1;
  if (result) fprintf(stderr,"Navigation frames do not match continuous tracking: %s\n",path);
  return result;
}
static const struct anchor *anchor_for(const struct channel *c,const struct record *r)
{
  const struct anchor *found=NULL;
  for (size_t j=0;j<c->anchors_count;++j) if (c->anchors[j].segment==r->segment&&
      c->anchors[j].frame.first_epoch<=r->epoch.epoch) found=&c->anchors[j];
  return found;
}
static void exclusion(FILE *file,unsigned long long index,int prn,const char *reason)
{ fprintf(file,"%llu,%d,%s\n",index,prn,reason); }
static const char *solve_status(int code)
{ return code==-1?"invalid_observation":code==-2?"insufficient_satellites":code==-3?"singular_geometry":code?"no_convergence":"ok"; }
static int process(struct channel *channels,const struct options *o,FILE *solutions,
                     FILE *observations,FILE *excluded,unsigned long long *total,unsigned long long *fixes)
{
  uint64_t first=0,end=0,step=(uint64_t)llround(o->fs*o->period_ms/1000);
  int ready=0,base_week=0,have_position=0; double local=NAN,previous_xyz[3]={0};
  for (int j=0;j<63;++j) if (channels[j].anchors_count) {
    const struct channel *c=&channels[j]; uint64_t a=c->anchors[0].frame.absolute_sample;
    if (a>=first) { first=a; base_week=(int)c->anchors[0].frame.ephemeris.WN; }
    const bds_b1c_tracking_epoch_t *e=&c->records[c->count-1].epoch;
    if (e->absolute_sample+e->sample_count>end) end=e->absolute_sample+e->sample_count;
    ++ready;
  }
  if (ready<4||first==UINT64_MAX||first+1>=end) return 0;
  ++first; uint64_t count=(end-first)/step;
  if (count>10000000) return -1;
  for (uint64_t index=0;index<count;++index) {
    uint64_t sample=first+index*step;
    bds_b1c_spp_observation_t obs[63]; bds_b1c_time_t times[63];
    const struct record *records[63]; size_t used=0; double max_time=-INFINITY;
    for (int j=0;j<63;++j) {
      const struct channel *c=&channels[j]; if (!c->anchors_count) continue;
      size_t k=locate(c,sample);
      if (k==SIZE_MAX||sample-c->records[k].epoch.absolute_sample>=c->records[k].epoch.sample_count||
          !c->records[k].epoch.valid) { exclusion(excluded,index,j+1,"tracking_unavailable"); continue; }
      const struct record *r=&c->records[k]; const struct anchor *a=anchor_for(c,r);
      if (!a) { exclusion(excluded,index,j+1,"no_continuous_anchor"); continue; }
      const bds_b1c_ephemeris_t *e=&a->frame.ephemeris;
      if (e->HS||e->DIF||e->SIF||e->AIF) { exclusion(excluded,index,j+1,"health_or_integrity"); continue; }
      bds_b1c_time_t tx; bds_b1c_satellite_t sat;
      if (bds_b1c_transmit_time(&a->frame,&r->epoch,sample,o->fs,&tx)) {
        exclusion(excluded,index,j+1,"invalid_time_model"); continue;
      }
      int code=bds_b1c_satellite_state(e,tx,o->max_age,&sat);
      if (code) { exclusion(excluded,index,j+1,code==-2?"stale_ephemeris":code==-4?"unsupported_geo":"invalid_satellite_model"); continue; }
      if (have_position) {
        double az,el;
        if (bds_b1c_azimuth_elevation(previous_xyz,sat.ecef_m,&az,&el)||el<o->elevation) {
          exclusion(excluded,index,j+1,"below_elevation_mask"); continue;
        }
      }
      obs[used]=(bds_b1c_spp_observation_t){j+1,sat,0}; times[used]=tx; records[used]=r;
      double t=((double)tx.week-base_week)*604800+tx.tow_s;
      if (t>max_time) max_time=t;
      ++used;
    }
    if (!isfinite(local)&&used>=4) local=max_time+o->offset_ms/1000;
    double before=local;
    for (size_t j=0;j<used;++j) {
      double t=((double)times[j].week-base_week)*604800+times[j].tow_s;
      obs[j].pseudorange_m=(before-t)*BDS_B1C_SPEED_OF_LIGHT;
    }
    bds_b1c_spp_solution_t s;
    int status=used<4?-2:bds_b1c_spp_solve(obs,used,o->tropo,&s);
    if (!status) {
      local-=s.clock_correction_m/BDS_B1C_SPEED_OF_LIGHT;
      memcpy(previous_xyz,s.ecef_m,sizeof(previous_xyz)); have_position=1; ++*fixes;
    }
    double correction=status?NAN:s.clock_correction_m;
    int carry=isfinite(local)?(int)floor(local/604800):0;
    fprintf(solutions,"%llu,%llu,%d,%.17g,%.17g,",(unsigned long long)index,(unsigned long long)sample,
        base_week+carry,before-carry*604800,local-carry*604800);
    if (status) fprintf(solutions,"nan,nan,nan,nan,nan,nan,nan,nan,nan,nan,nan,nan,nan,%zu,0,%s\n",used,solve_status(status));
    else fprintf(solutions,"%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%zu,1,ok\n",
        s.ecef_m[0],s.ecef_m[1],s.ecef_m[2],s.clock_correction_m,s.latitude_deg,s.longitude_deg,s.height_m,
        s.gdop,s.pdop,s.hdop,s.vdop,s.tdop,s.residual_rms_m,used);
    for (size_t j=0;j<used;++j) {
      const bds_b1c_tracking_epoch_t *e=&records[j]->epoch;
      double raw=obs[j].pseudorange_m,clock=obs[j].satellite.clock_correction_s;
      fprintf(observations,"%llu,%d,%llu,%llu,%llu,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d\n",
          (unsigned long long)index,obs[j].prn,(unsigned long long)e->generation,(unsigned long long)e->epoch,
          (unsigned long long)sample,times[j].week,times[j].tow_s,raw,obs[j].satellite.ecef_m[0],
          obs[j].satellite.ecef_m[1],obs[j].satellite.ecef_m[2],clock,raw+clock*BDS_B1C_SPEED_OF_LIGHT,
          raw+clock*BDS_B1C_SPEED_OF_LIGHT-correction,status?NAN:s.elevation_deg[j],status?NAN:s.azimuth_deg[j],
          status?NAN:s.troposphere_m[j],status?NAN:s.residual_m[j],!status);
    }
    ++*total;
    if (isfinite(local)) local+=(double)step/o->fs;
    if (ferror(solutions)||ferror(observations)||ferror(excluded)) return -1;
  }
  return 0;
}
static void usage(const char *p)
{
  fprintf(stderr,"Usage: %s --tracking-dir DIR --navigation-dir DIR --sample-rate HZ --output-dir NEW_DIR\n"
      "  --period-ms N              Default 200; integer sample interval\n"
      "  --elevation-mask DEG       Default 5, initialized after first fix\n"
      "  --start-offset-ms N        Initial clock guess, default 68.802; jointly solved\n"
      "  --max-ephemeris-age S      Default 7200, orbit and clock\n"
      "  --no-tropo                 Disable MATLAB's fixed-atmosphere correction\n"
      "Outputs solutions.csv, observations.csv, exclusions.csv, run.json.\n"
      "Offline retrospective SPP; MEO/IGSO, no ionosphere/RAIM.\n",p);
}
int main(int argc,char **argv)
{
  struct options o={0,200,5,68.802,7200,1};
  const char *tracking=NULL,*navigation=NULL,*directory=NULL;
  struct channel *channels=calloc(63,sizeof(*channels));
  FILE *solutions=NULL,*observations=NULL,*excluded=NULL,*meta=NULL;
  int error=0,channel_count=0; unsigned long long total=0,fixes=0;
  if (!channels) return 3;
  for (int j=1;j<argc;++j) {
    const char *name=argv[j];
    if (!strcmp(name,"--help")) { usage(argv[0]); free(channels); return 0; }
    if (!strcmp(name,"--no-tropo")) { o.tropo=0; continue; }
    if (j+1>=argc) goto invalid;
    const char *value=argv[++j];
    if (!strcmp(name,"--tracking-dir")) tracking=value;
    else if (!strcmp(name,"--navigation-dir")) navigation=value;
    else if (!strcmp(name,"--output-dir")) directory=value;
    else {
      double *target=!strcmp(name,"--sample-rate")?&o.fs:!strcmp(name,"--period-ms")?&o.period_ms:
          !strcmp(name,"--elevation-mask")?&o.elevation:!strcmp(name,"--start-offset-ms")?&o.offset_ms:
          !strcmp(name,"--max-ephemeris-age")?&o.max_age:NULL;
      if (!target||csv_number(value,target)) goto invalid;
    }
  }
  if (!tracking||!navigation||!directory||o.fs<=0||o.fs>1e9||o.period_ms<1||o.period_ms>10000||
      o.elevation<0||o.elevation>=90||o.offset_ms<=0||o.offset_ms>1000||o.max_age<=0||o.max_age>302400||
      fabs(o.fs*o.period_ms/1000-round(o.fs*o.period_ms/1000))>1e-7) goto invalid;
  for (int prn=1;prn<=63;++prn) {
    char path[4096]; struct stat st;
    if (snprintf(path,sizeof(path),"%s/prn_%02d.csv",tracking,prn)>=(int)sizeof(path)) { error=2; goto done; }
    if (stat(path,&st)) { if (errno==ENOENT) continue; error=3; goto done; }
    if (read_tracking(&channels[prn-1],prn,path,o.fs)) { error=3; goto done; }
    ++channel_count;
  }
  if (!channel_count||read_navigation(channels,navigation)) { error=3; goto done; }
  if (csv_new_directory(directory)) { fprintf(stderr,"Output must be a new directory: %s\n",directory); error=2; goto done; }
  solutions=csv_output(directory,"solutions.csv"); observations=csv_output(directory,"observations.csv");
  excluded=csv_output(directory,"exclusions.csv"); meta=csv_output(directory,"run.json");
  if (!solutions||!observations||!excluded||!meta) { error=3; goto done; }
  fputs("index,absolute_sample,receive_week,local_time_before_s,local_time_s,x_m,y_m,z_m,clock_correction_m,latitude_deg,longitude_deg,height_m,gdop,pdop,hdop,vdop,tdop,residual_rms_m,satellites,valid,status\n",solutions);
  fputs("index,prn,generation,epoch,absolute_sample,transmit_week,transmit_time_s,raw_pseudorange_m,satellite_x_m,satellite_y_m,satellite_z_m,satellite_clock_s,clock_corrected_pseudorange_m,receiver_corrected_pseudorange_m,elevation_deg,azimuth_deg,troposphere_m,residual_m,used\n",observations);
  fputs("index,prn,reason\n",excluded);
  if (process(channels,&o,solutions,observations,excluded,&total,&fixes)) error=3;
done:
  for (int j=0;j<63;++j) { free(channels[j].records); free(channels[j].anchors); }
  free(channels);
  if (csv_close(solutions)|csv_close(observations)|csv_close(excluded)) error=3;
  if (meta) {
    fputs("{\n  \"schema_version\": 1,\n  \"tracking_directory\": ",meta); csv_json_string(meta,tracking);
    fputs(",\n  \"navigation_directory\": ",meta); csv_json_string(meta,navigation);
    fprintf(meta,",\n  \"sample_rate_hz\": %.17g,\n  \"period_ms\": %.17g,\n  \"elevation_mask_deg\": %.17g,\n"
      "  \"start_offset_ms\": %.17g,\n  \"max_ephemeris_age_s\": %.17g,\n  \"troposphere\": %s,\n"
      "  \"ionosphere\": false,\n  \"offline_retrospective\": true,\n  \"epochs\": %llu,\n"
      "  \"valid_fixes\": %llu,\n  \"complete\": %s,\n  \"status_code\": %d\n}\n",o.fs,o.period_ms,
      o.elevation,o.offset_ms,o.max_age,o.tropo?"true":"false",total,fixes,error?"false":"true",error);
    if (csv_close(meta)) error=3;
    fprintf(stderr,"B1C SPP: %llu/%llu valid fixes\n",fixes,total);
  }
  return error;
invalid:
  usage(argv[0]); free(channels); return 2;
}

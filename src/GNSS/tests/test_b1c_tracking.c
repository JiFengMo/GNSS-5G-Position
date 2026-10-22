#include "bds5g/b1c_tracking.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS 4092000.0
#define PI 3.14159265358979323846
#define CAPACITY 500
struct results { bds_b1c_tracking_epoch_t epochs[CAPACITY]; size_t count; };
static void collect(const bds_b1c_tracking_epoch_t *r,void *context)
{
  struct results *out=context; assert(out->count<CAPACITY);
  out->epochs[out->count++]=*r;
}
static uint32_t rng=123456789;
static double uniform(void)
{
  rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
  return (rng+1.0)/4294967297.0;
}
static double gaussian(void) { return sqrt(-2*log(uniform()))*cos(2*PI*uniform()); }
static void synthesize(bds5g_cf32_t *samples,size_t count,uint64_t start,const int8_t *data,
                        const int8_t *pilot,const int8_t *secondary)
{
  for (size_t k=0;k<count;++k) {
    uint64_t n=start+k;
    double after=(double)n/FS>2.5 ? (double)n/FS-2.5 : 0;
    double chips=(double)n/FS * BDS_B1C_CHIP_RATE_HZ*(1+120.0/1575420000.0) +
                 after*BDS_B1C_CHIP_RATE_HZ*60.0/1575420000.0;
    int64_t half=(int64_t)ceil(chips*2)-1;
    unsigned index=(unsigned)((half%20460+20460)%20460);
    unsigned period=(unsigned)floor(chips/10230);
    double a=.0524404424*data[index/2]*(index&1 ? 1 : -1)*(period&1 ? -1 : 1);
    double b=.0851469318*pilot[index/2]*(index&1 ? 1 : -1)*secondary[period%1800];
    if (n>=(uint64_t)(1.5*FS) && n<(uint64_t)(2.5*FS)) a=b=0;
    double phase=2*PI*(20120.0*(double)n/FS+60*after)+.3;
    samples[k].i=(float)(a*cos(phase)-b*sin(phase)+.5*gaussian());
    samples[k].q=(float)(a*sin(phase)+b*cos(phase)+.5*gaussian());
  }
}
static void compare(const bds_b1c_tracking_epoch_t *a,const bds_b1c_tracking_epoch_t *b)
{
  assert(a->absolute_sample==b->absolute_sample && a->sample_count==b->sample_count);
  assert(a->epoch==b->epoch && a->generation==b->generation);
  assert(a->carrier_hz==b->carrier_hz && a->code_rate_hz==b->code_rate_hz);
  assert(a->data_prompt.i==b->data_prompt.i && a->data_prompt.q==b->data_prompt.q);
  assert(a->pilot_prompt.i==b->pilot_prompt.i && a->pilot_prompt.q==b->pilot_prompt.q);
  assert(a->rem_code_phase==b->rem_code_phase && a->rem_carrier_phase==b->rem_carrier_phase);
  assert(a->state==b->state && a->valid==b->valid);
}
static int write_fixture(const char *directory)
{
  char path[4096];
  snprintf(path,sizeof(path),"%s/synthetic.ci8",directory);
  FILE *file=fopen(path,"wb"); assert(file);
  int8_t data[10230],pilot[10230],secondary[1800],bytes[16382];
  assert(!bds_b1c_generate_code(23,BDS_B1C_DATA,data,sizeof(data)));
  assert(!bds_b1c_generate_code(23,BDS_B1C_PILOT,pilot,sizeof(pilot)));
  assert(!bds_b1c_generate_secondary_code(23,secondary,sizeof(secondary)));
  for (unsigned k=0;k<26000;++k) { fputc(0,file); fputc(0,file); }
  bds5g_cf32_t samples[8191];
  uint64_t total=(uint64_t)(4.05*FS);
  for (uint64_t n=0;n<total;) {
    size_t count=total-n<8191 ? (size_t)(total-n) : 8191;
    synthesize(samples,count,n,data,pilot,secondary);
    for (size_t k=0;k<count;++k) {
      double i=round(samples[k].i*32),q=round(samples[k].q*32);
      bytes[2*k]=(int8_t)fmax(-127,fmin(127,i));
      bytes[2*k+1]=(int8_t)fmax(-127,fmin(127,q));
    }
    assert(fwrite(bytes,2,count,file)==count); n+=count;
  }
  assert(!fclose(file));
  snprintf(path,sizeof(path),"%s/seeds.csv",directory); file=fopen(path,"w"); assert(file);
  fputs("prn,carrier_hz,doppler_hz,code_phase_samples,peak_metric,cn0_db_hz,acquired,coarse_doppler_hz,fine_metric,input_start_sample\n"
        "23,20125,125,26000,50,nan,1,125,1,0\n",file);
  assert(!fclose(file)); return 0;
}
int main(int argc,char **argv)
{
  if (argc==3 && !strcmp(argv[1],"--write-fixture")) return write_fixture(argv[2]);
  if (argc!=1) return 2;
  bds_b1c_tracking_options_t o; bds_b1c_tracking_defaults(&o);
  o.sample_rate_hz=FS; o.if_hz=20000; o.quality_epochs=10;
  bds_b1c_acquisition_t seed={.prn=23,.doppler_hz=125,.carrier_hz=20125,
                             .code_phase_samples=26000,.acquired=1};
  bds_b1c_tracker_t *a=NULL,*b=NULL;
  assert(!bds_b1c_tracker_create(&o,&seed,100000,&a));
  assert(!bds_b1c_tracker_create(&o,&seed,100000,&b));
  struct results *ra=calloc(1,sizeof(*ra)), *rb=calloc(1,sizeof(*rb)); assert(ra&&rb);
  int8_t data[10230],pilot[10230],secondary[1800];
  assert(!bds_b1c_generate_code(23,BDS_B1C_DATA,data,sizeof(data)));
  assert(!bds_b1c_generate_code(23,BDS_B1C_PILOT,pilot,sizeof(pilot)));
  assert(!bds_b1c_generate_secondary_code(23,secondary,sizeof(secondary)));
  const size_t block=8191;
  bds5g_cf32_t *samples=calloc(40920,sizeof(*samples)); assert(samples);
  const uint64_t total=(uint64_t)(4*FS), first=126000;
  for (uint64_t n=0;n<total;) {
    size_t count=total-n<block ? (size_t)(total-n) : block;
    synthesize(samples,count,n,data,pilot,secondary);
    assert(!bds_b1c_tracker_push(a,samples,count,first+n,collect,ra));
    size_t pos=0;
    while (pos<count) {
      size_t take=(pos%4==0 ? 1 : pos%4==1 ? 7 : pos%4==2 ? 1021 : 503);
      if (take>count-pos) take=count-pos;
      assert(!bds_b1c_tracker_push(b,samples+pos,take,first+n+pos,collect,rb));
      pos+=take;
    }
    n+=count;
  }
  assert(ra->count==rb->count && ra->count>=399 && ra->count<=400);
  unsigned locked=0,lost=0;
  for (size_t k=0;k<ra->count;++k) {
    const bds_b1c_tracking_epoch_t *r=&ra->epochs[k];
    compare(r,&rb->epochs[k]);
    assert(r->absolute_sample==first || k>0);
    if (k) {
      const bds_b1c_tracking_epoch_t *p=&ra->epochs[k-1];
      assert(r->absolute_sample==p->absolute_sample+p->sample_count);
      assert(fabs(r->carrier_cycles-p->carrier_cycles-p->carrier_hz*p->integration_s)<1e-8);
    }
    assert(r->sample_time_s==(double)r->absolute_sample/FS);
    assert(!r->valid || (r->state==BDS_B1C_LOCKED && r->quality_good));
    locked+=r->valid; lost+=r->state==BDS_B1C_LOST;
  }
  fprintf(stderr,"Synthetic tracking: %zu epochs, %u valid, %u lost, final Doppler %.3f\n",
          ra->count,locked,lost,ra->epochs[ra->count-1].doppler_hz);
  assert(locked>100 && lost>100);
  assert(ra->epochs[ra->count-1].state==BDS_B1C_LOST);
  assert(!ra->epochs[ra->count-1].valid);
  assert(fabs(ra->epochs[140].doppler_hz-120)<5);
  uint64_t next=bds_b1c_tracker_next_sample(a);
  assert(bds_b1c_tracker_push(a,samples,1,next+1,NULL,NULL)==-3);
  assert(bds_b1c_tracker_state(a)==BDS_B1C_LOST);
  assert(bds_b1c_tracker_push(a,samples,1,next,NULL,NULL)==-3);
  assert(!bds_b1c_tracker_reset(a,&seed,100000));
  next=bds_b1c_tracker_next_sample(a); samples[0].i=NAN;
  assert(bds_b1c_tracker_push(a,samples,1,next,NULL,NULL)==-1);
  assert(bds_b1c_tracker_next_sample(a)==next);
  assert(bds_b1c_tracker_push(a,NULL,1,next,NULL,NULL)==-1);
  assert(!bds_b1c_tracker_push(a,NULL,0,next,NULL,NULL));
  bds_b1c_tracker_destroy(a); bds_b1c_tracker_destroy(b);

  seed.doppler_hz=0; seed.carrier_hz=20000; seed.code_phase_samples=0;
  assert(!bds_b1c_tracker_create(&o,&seed,0,&a));
  memset(samples,0,40920*sizeof(*samples)); ra->count=0;
  assert(!bds_b1c_tracker_push(a,samples,40919,0,collect,ra));
  assert(ra->count==0 && bds_b1c_tracker_pending_samples(a)==40919);
  assert(!bds_b1c_tracker_push(a,samples,1,40919,collect,ra)); assert(ra->count==1);
  for (int k=1;k<20;++k)
    assert(!bds_b1c_tracker_push(a,samples,40920,(uint64_t)k*40920,collect,ra));
  assert(bds_b1c_tracker_state(a)==BDS_B1C_LOST && !ra->epochs[19].valid);
  assert(isnan(ra->epochs[19].cn0_db_hz));
  assert(!bds_b1c_tracker_reset(a,&seed,1000)); ra->count=0;
  assert(!bds_b1c_tracker_push(a,samples,40920,1000,collect,ra));
  assert(ra->epochs[0].generation==1 && ra->epochs[0].carrier_cycles==0);
  assert(ra->epochs[0].state==BDS_B1C_PULL_IN && !ra->epochs[0].valid);
  seed.acquired=0;
  assert(bds_b1c_tracker_reset(a,&seed,0)==-1);
  bds_b1c_tracker_destroy(a);
  free(samples); free(ra); free(rb);
  puts("PASS: chunk invariance, sample/carrier continuity, bit/secondary flips, noise, fade/recovery,");
  puts("pull-in/lock/loss, gap rejection, nonfinite transactions, partial epochs and reset generations");
  return 0;
}

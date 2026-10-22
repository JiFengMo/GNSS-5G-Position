#include "bds5g/b1c_position.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define C BDS_B1C_SPEED_OF_LIGHT
static void vectors(const char *path)
{
  FILE *f=fopen(path,"r"); assert(f); char line[4096]; unsigned count=0; double max_error=0;
  assert(fgets(line,sizeof(line),f));
  while (fgets(line,sizeof(line),f)) {
    char *cols[12],*token=strtok(line,",\r\n"); int n=0;
    while (token&&n<12) { cols[n++]=token; token=strtok(NULL,",\r\n"); }
    assert(n==12&&!token); uint8_t bits[878]; assert(strlen(cols[11])==878);
    for (size_t k=0;k<878;++k) bits[k]=(uint8_t)(cols[11][k]-'0');
    bds_b1c_ephemeris_t e; int prn=atoi(cols[0]);
    assert(!bds_b1c_decode_navigation(bits,878,prn,&e));
    e.SatType=(unsigned)atoi(cols[1]); e.t_oe=strtod(cols[4],NULL); e.t_oc=strtod(cols[5],NULL); e.TOW=strtod(cols[6],NULL);
    bds_b1c_time_t t={atoi(cols[2]),strtod(cols[3],NULL)}; bds_b1c_satellite_t s;
    assert(!bds_b1c_satellite_state(&e,t,7200,&s));
    for (int k=0;k<3;++k) {
      double error=fabs(s.ecef_m[k]-strtod(cols[7+k],NULL)); if (error>max_error) max_error=error;
      assert(error<2e-6);
    }
    assert(fabs(s.clock_correction_s-strtod(cols[10],NULL))<2e-15);
    bds_b1c_time_t old=t; ++old.week;
    assert(bds_b1c_satellite_state(&e,old,7200,&s)==-2);
    assert(!s.ecef_m[0]);
    if (!count) {
      e.valid=0; assert(bds_b1c_satellite_state(&e,t,7200,&s)==-1); e.valid=1;
      e.SatType=1; assert(bds_b1c_satellite_state(&e,t,7200,&s)==-4); e.SatType=3;
      e.e=1; assert(bds_b1c_satellite_state(&e,t,7200,&s)==-1);
      e.e=.999; e.M_0=.1; assert(bds_b1c_satellite_state(&e,t,7200,&s)==-3);
      e.deltaA=NAN; assert(bds_b1c_satellite_state(&e,t,7200,&s)==-1);
    }
    ++count;
  }
  assert(!ferror(f)&&!fclose(f)&&count>=100);
  printf("%u original MATLAB satellite vectors: max coordinate error %.3g m\n",count,max_error);
}
static void transmit(void)
{
  bds_b1c_nav_frame_t f={0}; bds_b1c_tracking_epoch_t e={0}; bds_b1c_time_t t;
  f.prn=23; f.generation=2; f.first_epoch=10; f.ephemeris.valid=1;
  f.ephemeris.PRN=23; f.ephemeris.WN=832; f.ephemeris.TOW=270342;
  e.prn=23; e.generation=2; e.epoch=11; e.absolute_sample=9007199254740993ULL;
  e.sample_count=180000; e.code_rate_hz=1023000; e.state=BDS_B1C_LOCKED; e.valid=1;
  assert(!bds_b1c_transmit_time(&f,&e,e.absolute_sample+90000,18e6,&t));
  assert(t.week==832&&t.tow_s==270342.015);
  f.ephemeris.TOW=604782; e.epoch=1810;
  assert(!bds_b1c_transmit_time(&f,&e,e.absolute_sample,18e6,&t)&&t.week==833&&t.tow_s==0);
  assert(bds_b1c_transmit_time(&f,&e,e.absolute_sample+180000,18e6,&t)==-1);
  e.generation=3; assert(bds_b1c_transmit_time(&f,&e,e.absolute_sample,18e6,&t)==-1);
  e.generation=2; e.valid=0; assert(bds_b1c_transmit_time(&f,&e,e.absolute_sample,18e6,&t)==-1);
  e.valid=1; e.code_rate_hz=NAN; assert(bds_b1c_transmit_time(&f,&e,e.absolute_sample,18e6,&t)==-1);
}
static void synthetic_spp(void)
{
  const double receiver[]={-1286600,-4721050,4079860};
  const double satellites[7][3]={{3663000,-26845000,6706000},{14079000,-21867000,-10158000},
    {-19320000,-20050000,-1754000},{-6286000,-22723000,14939000},
    {-9429000,-17155000,19909000},{10792000,-11867000,22830000},{-17299000,3782000,21558000}};
  bds_b1c_spp_observation_t obs[7]={0}; bds_b1c_spp_solution_t s;
  for (int j=0;j<7;++j) {
    obs[j].prn=j+1; memcpy(obs[j].satellite.ecef_m,satellites[j],3*sizeof(double));
    obs[j].satellite.clock_correction_s=(j-3)*1e-4;
    double sum=0; for (int k=0;k<3;++k) sum+=pow(satellites[j][k]-receiver[k],2);
    double theta=7.292115147e-5*sqrt(sum)/C;
    double rotated[]={cos(theta)*satellites[j][0]+sin(theta)*satellites[j][1],
      -sin(theta)*satellites[j][0]+cos(theta)*satellites[j][1],satellites[j][2]};
    sum=0; for (int k=0;k<3;++k) sum+=pow(rotated[k]-receiver[k],2);
    obs[j].pseudorange_m=sqrt(sum)+500000-C*obs[j].satellite.clock_correction_s;
  }
  assert(!bds_b1c_spp_solve(obs,7,0,&s)&&s.valid&&s.iterations==10);
  for (int k=0;k<3;++k) assert(fabs(s.ecef_m[k]-receiver[k])<1e-6);
  assert(fabs(s.clock_correction_m-500000)<1e-6&&s.residual_rms_m<1e-6);
  assert(s.hdop>0&&s.vdop>0&&fabs(s.hdop*s.hdop+s.vdop*s.vdop-s.pdop*s.pdop)<1e-12);
  for (int j=0;j<7;++j) obs[j].pseudorange_m+=12345;
  assert(!bds_b1c_spp_solve(obs,7,0,&s));
  for (int k=0;k<3;++k) assert(fabs(s.ecef_m[k]-receiver[k])<1e-6);
  assert(fabs(s.clock_correction_m-512345)<1e-6);
  assert(!bds_b1c_spp_solve(obs,4,0,&s)&&s.valid);
  assert(bds_b1c_spp_solve(obs,3,0,&s)==-2&&!s.valid);
  obs[0].pseudorange_m=-1; assert(bds_b1c_spp_solve(obs,7,0,&s)==-1&&!s.valid);
  obs[0]=obs[1]; assert(bds_b1c_spp_solve(obs,7,0,&s)==-1);
  for (int j=0;j<7;++j) {
    obs[j].prn=j+1; obs[j].pseudorange_m=2e7;
    memcpy(obs[j].satellite.ecef_m,satellites[0],3*sizeof(double));
  }
  assert(bds_b1c_spp_solve(obs,7,0,&s)==-3&&!s.valid);
  obs[0].satellite.ecef_m[1]=NAN; assert(bds_b1c_spp_solve(obs,7,0,&s)==-1);
  double phi,lambda,h,eq[]={6378137,0,0},pole[]={0,0,6356752.314245179};
  assert(!bds_b1c_ecef_to_geodetic(eq,&phi,&lambda,&h)&&fabs(phi)<1e-10&&fabs(lambda)<1e-10&&fabs(h)<1e-5);
  assert(!bds_b1c_ecef_to_geodetic(pole,&phi,&lambda,&h)&&fabs(phi-90)<1e-10&&fabs(h)<1e-5);
  memset(eq,0,sizeof(eq)); assert(bds_b1c_ecef_to_geodetic(eq,&phi,&lambda,&h)==-1);
}
int main(int argc,char **argv)
{ assert(argc==2); transmit(); synthetic_spp(); vectors(argv[1]); puts("B1C positioning tests passed"); return 0; }

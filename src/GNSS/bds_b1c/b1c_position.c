/* SPDX-License-Identifier: GPL-2.0-or-later
 * CU-SDR/SoftGNSS orbit, clock, pseudorange and SPP algorithms adapted from
 * Yafeng Li, Nagaraj C. Shivaramaiah, Dennis M. Akos; Kai Borre, Darius
 * Plausinaitis, Peter Rinder, Nicolaj Bertelsen and C. Goad.
 * See docs/positioning.md for conventions and supported models.
 */
#include "bds5g/b1c_position.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
#define BDS_PI 3.1415926535898
#define C BDS_B1C_SPEED_OF_LIGHT
static double wrap_week(double t)
{ if (t>302400) return t-604800; if (t< -302400) return t+604800; return t; }
static int finite3(const double *x)
{ return isfinite(x[0])&&isfinite(x[1])&&isfinite(x[2]); }
static double length3(const double *x)
{ return sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]); }
int bds_b1c_transmit_time(const bds_b1c_nav_frame_t *a,
                          const bds_b1c_tracking_epoch_t *e,uint64_t sample,
                          double fs,bds_b1c_time_t *out)
{
  if (out) memset(out,0,sizeof(*out));
  if (!out||!a||!e||!a->ephemeris.valid||a->status!=BDS_B1C_NAV_OK||
      e->prn!=a->prn||e->prn!=a->ephemeris.PRN||!e->valid||e->state!=BDS_B1C_LOCKED||
      e->generation!=a->generation||e->epoch<a->first_epoch||
      sample<e->absolute_sample||sample-e->absolute_sample>=e->sample_count||
      !isfinite(fs)||fs<=0||!isfinite(e->code_rate_hz)||e->code_rate_hz<1e6||e->code_rate_hz>1.05e6||
      !isfinite(e->rem_code_phase)||e->rem_code_phase<0||e->rem_code_phase>2||
      !isfinite(a->ephemeris.TOW)||a->ephemeris.TOW<0||a->ephemeris.TOW>=604800||
      e->epoch-a->first_epoch>60480000) return -1;
  double phase=e->rem_code_phase+(e->code_rate_hz/fs)*(double)(sample-e->absolute_sample);
  double t=(phase/10230.0+(double)(e->epoch-a->first_epoch))*.01+a->ephemeris.TOW;
  int weeks=(int)floor(t/604800.0);
  out->week=(int)a->ephemeris.WN+weeks; out->tow_s=t-weeks*604800.0;
  return 0;
}
int bds_b1c_satellite_state(const bds_b1c_ephemeris_t *e,bds_b1c_time_t t,
                            double max_age,bds_b1c_satellite_t *out)
{
  bds_b1c_satellite_t state={0};
  if (out) memset(out,0,sizeof(*out));
  if (!out||!e||!e->valid||e->PRN<1||e->PRN>63||t.week<0||
      !isfinite(t.tow_s)||t.tow_s<0||t.tow_s>=604800||!isfinite(max_age)||max_age<=0||max_age>302400) return -1;
  if (e->SatType==1) return -4;
  if (e->SatType!=2&&e->SatType!=3) return -1;
  double fields[]={e->TOW,e->t_oe,e->t_oc,e->deltaA,e->ADot,e->e,e->M_0,e->omega,
    e->omega_0,e->i_0,e->omegaDot,e->i_0Dot,e->delta_n_0,e->delta_n_0Dot,
    e->C_uc,e->C_us,e->C_rc,e->C_rs,e->C_ic,e->C_is,e->a_0,e->a_1,e->a_2,e->T_GDB1Cp};
  for (size_t k=0;k<sizeof(fields)/sizeof(*fields);++k) if (!isfinite(fields[k])) return -1;
  if (e->e<0||e->e>=1||e->t_oe<0||e->t_oe>=604800||e->t_oc<0||e->t_oc>=604800||
      e->TOW<0||e->TOW>=604800) return -1;
  /* Interpret toe/toc in the week nearest their own broadcast frame, then
   * measure age without wrapping away a whole stale week. */
  double since_frame=((double)t.week-e->WN)*604800+(t.tow_s-e->TOW);
  double dt=since_frame+wrap_week(e->TOW-e->t_oc);
  double tk0=since_frame+wrap_week(e->TOW-e->t_oe);
  if (fabs(dt)>max_age||fabs(tk0)>max_age) return -2;
  double clock=(e->a_2*dt+e->a_1)*dt+e->a_0-e->T_GDB1Cp;
  double tk=wrap_week((t.tow_s-clock)-e->t_oe);
  double a0=(e->SatType==3?27906100.:42162200.)+e->deltaA;
  double a=a0+e->ADot*tk;
  if (a0<1e7||a0>6e7||a<1e7||a>6e7||fabs(clock)>.1) return -1;
  double n0=sqrt(3.986004418e14/(a0*a0*a0));
  double delta_n=e->delta_n_0+.5*e->delta_n_0Dot*tk;
  double mean=fmod(e->M_0+(n0+delta_n)*tk+2*BDS_PI,2*BDS_PI);
  double eccentric=mean; int converged=0;
  for (int j=0;j<10;++j) {
    double previous=eccentric; eccentric=mean+e->e*sin(eccentric);
    if (fabs(fmod(eccentric-previous,2*BDS_PI))<1e-12) { converged=1; break; }
  }
  if (!converged) return -3;
  eccentric=fmod(eccentric+2*BDS_PI,2*BDS_PI);
  double rel=-4.44280730904398e-10*e->e*sqrt(a0)*sin(eccentric);
  double nu=atan2(sqrt(1-e->e*e->e)*sin(eccentric),cos(eccentric)-e->e);
  double phi=fmod(nu+e->omega,2*BDS_PI),c2=cos(2*phi),s2=sin(2*phi);
  double arg=phi+e->C_uc*c2+e->C_us*s2;
  double radius=a*(1-e->e*cos(eccentric))+e->C_rc*c2+e->C_rs*s2;
  double inclination=e->i_0+e->i_0Dot*tk+e->C_ic*c2+e->C_is*s2;
  double node=fmod(e->omega_0+(e->omegaDot-7.2921150e-5)*tk-7.2921150e-5*e->t_oe+2*BDS_PI,2*BDS_PI);
  double xp=radius*cos(arg),yp=radius*sin(arg);
  state.ecef_m[0]=xp*cos(node)-yp*cos(inclination)*sin(node);
  state.ecef_m[1]=xp*sin(node)+yp*cos(inclination)*cos(node);
  state.ecef_m[2]=yp*sin(inclination);
  state.clock_correction_s=clock+rel; state.relativistic_s=rel;
  state.orbit_age_s=tk; state.clock_age_s=dt;
  if (!finite3(state.ecef_m)||!isfinite(state.clock_correction_s)) return -3;
  *out=state; return 0;
}
static int geodetic(const double *xyz,double finv,double *lat,double *lon,double *h)
{
  if (!finite3(xyz)||length3(xyz)<1) return -1;
  double p=hypot(xyz[0],xyz[1]),esq=(2-1/finv)/finv;
  double phi=atan2(xyz[2],p),height=0;
  for (int j=0;j<20;++j) {
    double sn=sin(phi),cs=cos(phi),n=6378137./sqrt(1-esq*sn*sn);
    double dp=p-(n+height)*cs,dz=xyz[2]-(n*(1-esq)+height)*sn;
    height+=sn*dz+cs*dp;
    phi+=(cs*dz-sn*dp)/(n+height);
    if (dp*dp+dz*dz<1e-10) { *lat=phi; *lon=atan2(xyz[1],xyz[0]); *h=height; return 0; }
  }
  return -1;
}
int bds_b1c_ecef_to_geodetic(const double xyz[3],double *lat,double *lon,double *height)
{
  double phi,lambda,h;
  if (!xyz||!lat||!lon||!height||geodetic(xyz,298.257223563,&phi,&lambda,&h)) return -1;
  *lat=phi*180/PI; *lon=lambda*180/PI; *height=h; return 0;
}
int bds_b1c_azimuth_elevation(const double receiver[3],const double sat[3],double *az,double *el)
{
  double phi,lambda,h;
  if (!receiver||!sat||!az||!el||!finite3(sat)||geodetic(receiver,298.257222101,&phi,&lambda,&h)) return -1;
  double dx=sat[0]-receiver[0],dy=sat[1]-receiver[1],dz=sat[2]-receiver[2];
  double east=-sin(lambda)*dx+cos(lambda)*dy;
  double north=-sin(phi)*cos(lambda)*dx-sin(phi)*sin(lambda)*dy+cos(phi)*dz;
  double up=cos(phi)*cos(lambda)*dx+cos(phi)*sin(lambda)*dy+sin(phi)*dz;
  double horizontal=hypot(east,north);
  *az=horizontal<1e-20?0:atan2(east,north)*180/PI;
  if (*az<0) *az+=360;
  *el=horizontal<1e-20?90:atan2(up,horizontal)*180/PI;
  return 0;
}
static double tropo(double sine)
{
  /* Original fixed atmosphere: h=0 km, p=1013 mb, T=293 K, RH=50%. */
  const double a_e=6378.137,b0=7.839257e-5,temperature=293.;
  double e0=.0611*50*pow(10,7.5*(temperature-273.15)/(237.3+temperature-273.15));
  double total=0;
  if (sine<0) sine=0;
  for (int wet=0;wet<2;++wet) {
    double refsea=wet ? (371900e-6/temperature-12.92e-6)/temperature : 77.624e-6/temperature;
    double top=wet ? 1.1385e-5*(1255/temperature+.05)/refsea : 1.1385e-5/refsea;
    double ref=refsea*(wet?e0:1013.);
    double rtop=sqrt(fmax(0,(a_e+top)*(a_e+top)-a_e*a_e*(1-sine*sine)))-a_e*sine;
    double a=-sine/top,b=-b0*(1-sine*sine)/top;
    double alpha[]={2*a,2*a*a+4*b/3,a*(a*a+3*b),pow(a,4)/5+2.4*a*a*b+1.2*b*b,
      2*a*b*(a*a+3*b)/3,b*b*(6*a*a+4*b)*1.428571e-1,0,0};
    if (b*b>1e-35) { alpha[6]=a*b*b*b/2; alpha[7]=pow(b,4)/9; }
    double dr=rtop,power=rtop*rtop;
    for (int j=0;j<8;++j) { dr+=alpha[j]*power; power*=rtop; }
    total+=dr*ref*1000;
  }
  return total;
}
static void rotate(const double *sat,const double *receiver,double *out)
{
  double delta[]={sat[0]-receiver[0],sat[1]-receiver[1],sat[2]-receiver[2]};
  double angle=7.292115147e-5*length3(delta)/C;
  out[0]=cos(angle)*sat[0]+sin(angle)*sat[1];
  out[1]=-sin(angle)*sat[0]+cos(angle)*sat[1]; out[2]=sat[2];
}
/* Householder QR, no normal-equation squaring of the condition number. */
static int qr(double a[63][4],double b[63],size_t count,double *delta,double q[4][4])
{
  double scale=0;
  for (int col=0;col<4;++col) {
    double norm=0,v[63]={0},dot;
    for (size_t row=(size_t)col;row<count;++row) norm=hypot(norm,a[row][col]);
    if (col==0) scale=norm;
    if (!isfinite(norm)||norm<=fmax(1e-12,scale*1e-10)) return -1;
    double alpha=-copysign(norm,a[col][col]);
    for (size_t row=(size_t)col;row<count;++row) v[row]=a[row][col];
    v[col]-=alpha; norm=0;
    for (size_t row=(size_t)col;row<count;++row) norm=hypot(norm,v[row]);
    for (size_t row=(size_t)col;row<count;++row) v[row]/=norm;
    for (int k=col;k<4;++k) {
      dot=0; for (size_t row=(size_t)col;row<count;++row) dot+=v[row]*a[row][k];
      for (size_t row=(size_t)col;row<count;++row) a[row][k]-=2*v[row]*dot;
    }
    dot=0; for (size_t row=(size_t)col;row<count;++row) dot+=v[row]*b[row];
    for (size_t row=(size_t)col;row<count;++row) b[row]-=2*v[row]*dot;
  }
  for (int row=3;row>=0;--row) {
    double value=b[row];
    for (int k=row+1;k<4;++k) value-=a[row][k]*delta[k];
    delta[row]=value/a[row][row];
  }
  if (q) {
    double inv[4][4]={{0}};
    for (int col=0;col<4;++col) for (int row=3;row>=0;--row) {
      double value=row==col?1:0;
      for (int k=row+1;k<4;++k) value-=a[row][k]*inv[k][col];
      inv[row][col]=value/a[row][row];
    }
    for (int row=0;row<4;++row) for (int col=0;col<4;++col) {
      q[row][col]=0; for (int k=0;k<4;++k) q[row][col]+=inv[row][k]*inv[col][k];
    }
  }
  return 0;
}
int bds_b1c_spp_solve(const bds_b1c_spp_observation_t *obs,size_t count,int use_tropo,
                      bds_b1c_spp_solution_t *out)
{
  bds_b1c_spp_solution_t s={0}; double pos[4]={0},q[4][4],last_delta=INFINITY;
  if (out) memset(out,0,sizeof(*out));
  if (!out||!obs||count>63||(use_tropo!=0&&use_tropo!=1)) return -1;
  if (count<4) return -2;
  for (size_t j=0;j<count;++j) {
    if (obs[j].prn<1||obs[j].prn>63||!finite3(obs[j].satellite.ecef_m)||
        length3(obs[j].satellite.ecef_m)<1e7||!isfinite(obs[j].pseudorange_m)||
        obs[j].pseudorange_m<=0||!isfinite(obs[j].satellite.clock_correction_s)) return -1;
    for (size_t k=0;k<j;++k) if (obs[k].prn==obs[j].prn) return -1;
  }
  for (unsigned iter=0;iter<10;++iter) {
    double a[63][4]={{0}},b[63]={0},delta[4]={0};
    for (size_t j=0;j<count;++j) {
      double sat[3],troposphere=2;
      if (!iter) memcpy(sat,obs[j].satellite.ecef_m,sizeof(sat));
      else {
        rotate(obs[j].satellite.ecef_m,pos,sat);
        if (bds_b1c_azimuth_elevation(pos,sat,&s.azimuth_deg[j],&s.elevation_deg[j])) return -4;
        troposphere=use_tropo?tropo(sin(s.elevation_deg[j]*PI/180)):0;
      }
      double diff[]={pos[0]-sat[0],pos[1]-sat[1],pos[2]-sat[2]},range=length3(diff);
      if (range<1||!isfinite(range)) return -4;
      for (int k=0;k<3;++k) a[j][k]=diff[k]/range;
      a[j][3]=1;
      b[j]=obs[j].pseudorange_m+obs[j].satellite.clock_correction_s*C-range-pos[3]-troposphere;
      s.troposphere_m[j]=troposphere;
    }
    if (qr(a,b,count,delta,q)) return -3;
    for (int k=0;k<4;++k) pos[k]+=delta[k];
    last_delta=fmax(length3(delta),fabs(delta[3]));
    if (!finite3(pos)||!isfinite(pos[3])) return -4;
  }
  if (last_delta>.001||length3(pos)<6e6||length3(pos)>7e6) return -4;
  memcpy(s.ecef_m,pos,sizeof(s.ecef_m)); s.clock_correction_m=pos[3];
  if (bds_b1c_ecef_to_geodetic(pos,&s.latitude_deg,&s.longitude_deg,&s.height_m)) return -4;
  s.gdop=sqrt(q[0][0]+q[1][1]+q[2][2]+q[3][3]);
  s.pdop=sqrt(q[0][0]+q[1][1]+q[2][2]); s.tdop=sqrt(q[3][3]);
  double lat=s.latitude_deg*PI/180,lon=s.longitude_deg*PI/180;
  double up[]={cos(lat)*cos(lon),cos(lat)*sin(lon),sin(lat)},vu=0;
  for (int j=0;j<3;++j) for (int k=0;k<3;++k) vu+=up[j]*q[j][k]*up[k];
  s.vdop=sqrt(fmax(0,vu)); s.hdop=sqrt(fmax(0,s.pdop*s.pdop-vu));
  double sum=0;
  for (size_t j=0;j<count;++j) {
    double sat[3]; rotate(obs[j].satellite.ecef_m,pos,sat);
    bds_b1c_azimuth_elevation(pos,sat,&s.azimuth_deg[j],&s.elevation_deg[j]);
    double tr=use_tropo?tropo(sin(s.elevation_deg[j]*PI/180)):0;
    double d[]={sat[0]-pos[0],sat[1]-pos[1],sat[2]-pos[2]};
    s.residual_m[j]=obs[j].pseudorange_m+C*obs[j].satellite.clock_correction_s-length3(d)-pos[3]-tr;
    s.troposphere_m[j]=tr; sum+=s.residual_m[j]*s.residual_m[j];
  }
  s.residual_rms_m=sqrt(sum/count); s.satellites=(unsigned)count; s.iterations=10; s.valid=1;
  *out=s; return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later
 * Adapted from CU-SDR BCNAV1decoding.m, BCH21_6Decoding.m,
 * BCH51_8Decoding.m and ephemeris.m by Yafeng Li, Nagaraj C. Shivaramaiah,
 * Dennis M. Akos and the SoftGNSS contributors. See docs/navigation.md.
 */
#include "bds5g/b1c_navigation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Bit offsets in the field parser are one-based, like the MATLAB source. */
static uint64_t u(const uint8_t *b, size_t first, unsigned width)
{
  uint64_t v=0;
  for (unsigned j=0;j<width;++j) v=(v<<1)|b[first-1+j];
  return v;
}
static double s(const uint8_t *b, size_t first, unsigned width)
{
  uint64_t v=u(b,first,width);
  return (double)((int64_t)v-((v>>(width-1)) ? (INT64_C(1)<<width) : 0));
}
static int binary(const uint8_t *b, size_t n)
{
  if (!b) return 0;
  for (size_t j=0;j<n;++j) if (b[j]>1) return 0;
  return 1;
}
uint32_t bds_b1c_crc24q(const uint8_t *b, size_t n)
{
  uint32_t crc=0;
  if (!binary(b,n)) return UINT32_MAX;
  for (size_t j=0;j<n;++j) {
    unsigned feedback=(crc>>23)^b[j];
    crc=(crc<<1)&0xffffffU;
    if (feedback) crc^=0x864cfbU;
  }
  return crc;
}
static void bch_encode(unsigned value, unsigned width, uint8_t *out, size_t n)
{
  unsigned mask=width==6 ? 0x3aU : 0xf9U;
  for (size_t j=0;j<n;++j) {
    out[j]=(uint8_t)((value>>(width-1))&1U);
    unsigned taps=value&mask, feedback=0;
    while (taps) { feedback^=taps&1U; taps>>=1; }
    value=((value<<1)|feedback)&((1U<<width)-1);
  }
}
int bds_b1c_bch_decode(const uint8_t *b, size_t n)
{
  uint8_t code[51];
  unsigned width=n==21 ? 6 : 8;
  if ((n!=21 && n!=51)||!binary(b,n)) return -1;
  for (unsigned value=0;value<(1U<<width);++value) {
    bch_encode(value,width,code,n);
    if (!memcmp(code,b,n)) return (int)value;
  }
  return -1;
}
int bds_b1c_decode_navigation(const uint8_t *b, size_t n, int prn,
                              bds_b1c_ephemeris_t *out)
{
  bds_b1c_ephemeris_t e={0};
  const uint8_t *p;
  const double pi=3.1415926535898;
  if (out) memset(out,0,sizeof(*out));
  if (!out||n!=878||prn<1||prn>63||!binary(b,n)) return -1;
  if (bds_b1c_crc24q(b+14,600)||bds_b1c_crc24q(b+614,264)) return -2;
  e.PRN=(int)u(b,1,6); e.SOH=(unsigned)u(b,7,8)*18;
  p=b+14;
  e.WN=(unsigned)u(p,1,13); e.HOW=(unsigned)u(p,14,8);
  e.IODC=(unsigned)u(p,22,10); /* ICD 10 bits; MATLAB mistakenly uses 11. */
  e.IODE=(unsigned)u(p,32,8);
  p=b+53;
  e.t_oe=(double)u(p,1,11)*300; e.SatType=(unsigned)u(p,12,2);
  e.deltaA=ldexp(s(p,14,26),-9); e.ADot=ldexp(s(p,40,25),-21);
  e.delta_n_0=ldexp(s(p,65,17),-44)*pi;
  e.delta_n_0Dot=ldexp(s(p,82,23),-57)*pi;
  e.M_0=ldexp(s(p,105,33),-32)*pi;
  e.e=ldexp((double)u(p,138,33),-34);
  e.omega=ldexp(s(p,171,33),-32)*pi;
  p=b+256;
  e.omega_0=ldexp(s(p,1,33),-32)*pi; e.i_0=ldexp(s(p,34,33),-32)*pi;
  e.omegaDot=ldexp(s(p,67,19),-44)*pi; e.i_0Dot=ldexp(s(p,86,15),-44)*pi;
  e.C_is=ldexp(s(p,101,16),-30); e.C_ic=ldexp(s(p,117,16),-30);
  e.C_rs=ldexp(s(p,133,24),-8); e.C_rc=ldexp(s(p,157,24),-8);
  e.C_us=ldexp(s(p,181,21),-30); e.C_uc=ldexp(s(p,202,21),-30);
  p=b+478;
  e.t_oc=(double)u(p,1,11)*300; e.a_0=ldexp(s(p,12,25),-34);
  e.a_1=ldexp(s(p,37,22),-50); e.a_2=ldexp(s(p,59,11),-66);
  p=b+547;
  e.T_GDB2ap=ldexp(s(p,1,12),-34); e.ISC_B1Cd=ldexp(s(p,13,12),-34);
  e.T_GDB1Cp=ldexp(s(p,25,12),-34);
  p=b+614;
  e.PageID=(unsigned)u(p,1,6); e.HS=(unsigned)u(p,7,2);
  e.DIF=(unsigned)u(p,9,1); e.SIF=(unsigned)u(p,10,1);
  e.AIF=(unsigned)u(p,11,1); e.SISMAI=(unsigned)u(p,12,4);
  e.alpha1=e.alpha2=e.alpha3=e.alpha4=e.alpha5=e.alpha6=e.alpha7=e.alpha8=e.alpha9=NAN;
  e.A_0UTC=e.A_1UTC=e.A_2UTC=e.delta_t_LS=e.t_ot=e.WN_ot=e.WN_LSF=e.DN=e.delta_t_LSF=NAN;
  e.GNSS_ID=e.WN_0BGTO=e.t_0BGTO=e.A_0BGTO=e.A_1BGTO=e.A_2BGTO=NAN;
  if (e.PageID==1) {
    const uint8_t *q=p+42;
    e.alpha1=ldexp((double)u(q,1,10),-3); e.alpha2=ldexp(s(q,11,8),-3);
    e.alpha3=ldexp((double)u(q,19,8),-3); e.alpha4=ldexp((double)u(q,27,8),-3);
    e.alpha5=ldexp((double)u(q,35,8),-3); e.alpha6=ldexp(s(q,43,8),-3);
    e.alpha7=ldexp(s(q,51,8),-3); e.alpha8=ldexp(s(q,59,8),-3);
    e.alpha9=ldexp(s(q,67,8),-3);
    q=p+116;
    e.A_0UTC=ldexp(s(q,1,16),-35); e.A_1UTC=ldexp(s(q,17,13),-51);
    e.A_2UTC=ldexp(s(q,30,7),-68); e.delta_t_LS=s(q,37,8);
    e.t_ot=(double)u(q,45,16)*16; e.WN_ot=(double)u(q,61,13);
    e.WN_LSF=(double)u(q,74,13); e.DN=(double)u(q,87,3);
    e.delta_t_LSF=s(q,90,8);
  } else if (e.PageID==3) {
    const uint8_t *q=p+158;
    e.GNSS_ID=(double)u(q,1,3); e.WN_0BGTO=(double)u(q,4,13);
    e.t_0BGTO=(double)u(q,17,16)*16; e.A_0BGTO=ldexp(s(q,33,16),-35);
    e.A_1BGTO=ldexp(s(q,49,13),-51); e.A_2BGTO=ldexp(s(q,62,7),-68);
  }
  e.TOW=e.HOW*3600.0+e.SOH;
  if (e.PRN!=prn||e.HOW>=168||e.SOH>=3600||e.SatType<1||e.SatType>3||
      e.t_oe>=604800||e.t_oc>=604800) return -3;
  e.valid=1;
  *out=e;
  return 0;
}
int bds_b1c_decode_frame(const uint8_t *symbols, size_t n, int prn,
                         bds_b1c_nav_frame_t *f)
{
  uint8_t b[1800],sf2[1200],sf3[528];
  size_t n2=0,n3=0;
  int p,soh;
  if (f) memset(f,0,sizeof(*f));
  if (!f||n!=1800||prn<1||prn>63||!binary(symbols,n)) return -1;
  f->prn=prn;
  memcpy(b,symbols,n);
  p=bds_b1c_bch_decode(b,21);
  if (p<0) {
    for (size_t j=0;j<n;++j) b[j]^=1;
    f->inverted=1;
    p=bds_b1c_bch_decode(b,21);
  }
  if (p<0) { f->status=BDS_B1C_NAV_BCH21; return 0; }
  soh=bds_b1c_bch_decode(b+21,51);
  if (soh<0) { f->status=BDS_B1C_NAV_BCH51; return 0; }
  for (unsigned j=0;j<6;++j) f->bits[j]=(uint8_t)((p>>(5-j))&1);
  for (unsigned j=0;j<8;++j) f->bits[6+j]=(uint8_t)((soh>>(7-j))&1);
  for (size_t row=0;row<36;++row) for (size_t col=0;col<48;++col) {
    uint8_t bit=b[72+36*col+row];
    if (row>=2&&row<=32&&row%3==2) sf3[n3++]=bit;
    else sf2[n2++]=bit;
  }
  memcpy(f->bits+14,sf2,600); memcpy(f->bits+614,sf3,264);
  f->crc2_ok=bds_b1c_crc24q(sf2,600)==0;
  f->crc3_ok=bds_b1c_crc24q(sf3,264)==0;
  if (p!=prn) f->status=BDS_B1C_NAV_PRN;
  else if (!f->crc2_ok) f->status=BDS_B1C_NAV_CRC2;
  else if (!f->crc3_ok) f->status=BDS_B1C_NAV_CRC3;
  else if (bds_b1c_decode_navigation(f->bits,878,prn,&f->ephemeris)) f->status=BDS_B1C_NAV_FIELDS;
  else f->status=BDS_B1C_NAV_OK;
  return 0;
}
const char *bds_b1c_nav_status_name(bds_b1c_nav_status_t status)
{
  static const char *names[]={"ok","bch21_failed","bch51_failed","prn_mismatch",
                             "crc2_failed","crc3_failed","invalid_fields"};
  return status>=BDS_B1C_NAV_OK&&status<=BDS_B1C_NAV_FIELDS ? names[status] : "unknown";
}
struct nav_symbol { uint64_t epoch,sample; uint8_t data; int8_t pilot; };
struct bds_b1c_navigator {
  int prn;
  double fs;
  int8_t secondary[1800];
  struct nav_symbol ring[1800];
  size_t head,count;
  uint64_t generation,next_epoch,next_sample;
};
int bds_b1c_navigator_create(int prn, double fs, bds_b1c_navigator_t **out)
{
  bds_b1c_navigator_t *n;
  if (out) *out=NULL;
  if (!out||prn<1||prn>63||!isfinite(fs)||fs<=0) return -1;
  n=calloc(1,sizeof(*n));
  if (!n) return -2;
  n->prn=prn; n->fs=fs;
  bds_b1c_generate_secondary_code(prn,n->secondary,1800);
  *out=n;
  return 0;
}
void bds_b1c_navigator_destroy(bds_b1c_navigator_t *n) { free(n); }
void bds_b1c_navigator_reset(bds_b1c_navigator_t *n)
{ if (n) n->head=n->count=0; }
int bds_b1c_navigator_push(bds_b1c_navigator_t *n,
                           const bds_b1c_tracking_epoch_t *e,
                           bds_b1c_nav_callback_t callback, void *context)
{
  int correlation=0;
  bds_b1c_nav_frame_t f;
  uint8_t symbols[1800];
  if (!n) return -1;
  if (!e||e->prn!=n->prn||!isfinite(e->data_prompt.i)||!isfinite(e->pilot_prompt.q)||
      !e->sample_count||e->sample_count>UINT64_MAX-e->absolute_sample||e->epoch==UINT64_MAX||
      !isfinite(e->integration_s)||e->integration_s<.009||e->integration_s>.011||
      fabs((double)e->sample_count/n->fs-e->integration_s)>1e-9) {
    bds_b1c_navigator_reset(n); return -1;
  }
  if (!e->valid||e->state!=BDS_B1C_LOCKED) { bds_b1c_navigator_reset(n); return 0; }
  if (n->count&&(e->generation!=n->generation||e->epoch!=n->next_epoch||
                e->absolute_sample!=n->next_sample)) bds_b1c_navigator_reset(n);
  n->generation=e->generation; n->next_epoch=e->epoch+1;
  n->next_sample=e->absolute_sample+e->sample_count;
  n->ring[n->head]=(struct nav_symbol){e->epoch,e->absolute_sample,
                                     e->data_prompt.i>0,e->pilot_prompt.q>0 ? 1 : -1};
  n->head=(n->head+1)%1800;
  if (n->count<1800) ++n->count;
  if (n->count<1800) return 0;
  for (size_t j=0;j<1800;++j) {
    const struct nav_symbol *v=&n->ring[(n->head+j)%1800];
    correlation+=v->pilot*n->secondary[j]; symbols[j]=v->data;
  }
  if (abs(correlation)!=1800) return 0;
  bds_b1c_decode_frame(symbols,1800,n->prn,&f);
  f.generation=n->generation; f.first_epoch=n->ring[n->head].epoch;
  f.absolute_sample=n->ring[n->head].sample; f.end_sample=n->next_sample;
  f.sample_time_s=(double)f.absolute_sample/n->fs;
  f.secondary_correlation=correlation;
  if (callback) callback(&f,context);
  return 0;
}

#include "bds5g/b1c_navigation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put(uint8_t *b,size_t first,unsigned width,int64_t value)
{
  for (unsigned j=0;j<width;++j) b[first-1+j]=(uint8_t)(((uint64_t)value>>(width-1-j))&1U);
}
static void append_crc(uint8_t *b,size_t n)
{ put(b,n-23,24,bds_b1c_crc24q(b,n-24)); assert(!bds_b1c_crc24q(b,n)); }
/* Array-register implementation follows the reference's circshift; different
 * representation from the integer register in the production decoder. */
static void encode(unsigned v,int width,uint8_t *out,int count)
{
  int reg[8],taps6[]={1,3,4,5},taps8[]={0,3,4,5,6,7};
  int *taps=width==6?taps6:taps8,nt=width==6?4:6;
  for (int k=0;k<width;++k) reg[k]=(v>>k)&1;
  for (int k=0;k<count;++k) {
    out[k]=(uint8_t)reg[width-1]; int fb=0;
    for (int j=0;j<nt;++j) fb^=reg[taps[j]];
    for (int j=width-1;j>0;--j) reg[j]=reg[j-1];
    reg[0]=fb;
  }
}
static void interleave(const uint8_t *b,uint8_t *symbols)
{
  uint8_t sf2[1200]={0},sf3[528]={0};
  memcpy(sf2,b+14,600); memcpy(sf3,b+614,264);
  unsigned prn=0,soh=0;
  for (int j=0;j<6;++j) prn=2*prn+b[j];
  for (int j=6;j<14;++j) soh=2*soh+b[j];
  encode(prn,6,symbols,21); encode(soh,8,symbols+21,51);
  /* Write each subframe in rows, then transmit the 36x48 block by column. */
  size_t n2=0,n3=0;
  for (int row=0;row<36;++row) for (int col=0;col<48;++col)
    symbols[72+36*col+row]=row>=2&&row<=32&&row%3==2 ? sf3[n3++] : sf2[n2++];
}
static void payload(uint8_t *b,int page)
{
  memset(b,0,878); put(b,1,6,23); put(b,7,8,4);
  uint8_t *p=b+14;
  put(p,1,13,8191); put(p,14,8,167); put(p,22,10,0x301); put(p,32,8,0xa5);
  p=b+53;
  put(p,1,11,2015); put(p,12,2,3); put(p,14,26,-33554432); put(p,40,25,16777215);
  put(p,65,17,-65536); put(p,82,23,-4194304);
  put(p,105,33,-INT64_C(4294967296)); put(p,138,33,INT64_C(8589934591));
  put(p,171,33,INT64_C(4294967295));
  p=b+256;
  put(p,1,33,-INT64_C(4294967296)); put(p,34,33,INT64_C(4294967295));
  put(p,67,19,-262144); put(p,86,15,16383); put(p,101,16,-32768);
  put(p,117,16,32767); put(p,133,24,-8388608); put(p,157,24,8388607);
  put(p,181,21,-1048576); put(p,202,21,1048575);
  p=b+478; put(p,1,11,2015); put(p,12,25,-16777216); put(p,37,22,-2097152); put(p,59,11,-1024);
  p=b+547; put(p,1,12,-2048); put(p,13,12,2047); put(p,25,12,-1);
  p=b+614; put(p,1,6,page); put(p,7,2,3); put(p,9,1,1); put(p,10,1,1); put(p,11,1,1); put(p,12,4,15);
  if (page==1) {
    uint8_t *q=p+42; put(q,1,10,1023); put(q,11,8,-128); put(q,19,8,255);
    put(q,27,8,254); put(q,35,8,253); put(q,43,8,-127); put(q,51,8,127); put(q,59,8,-1); put(q,67,8,-2);
    q=p+116; put(q,1,16,-32768); put(q,17,13,-4096); put(q,30,7,-64); put(q,37,8,-128);
    put(q,45,16,65535); put(q,61,13,8191); put(q,74,13,8190); put(q,87,3,7); put(q,90,8,127);
  } else if (page==3) {
    uint8_t *q=p+158; put(q,1,3,7); put(q,4,13,8191); put(q,17,16,65535);
    put(q,33,16,-32768); put(q,49,13,-4096); put(q,62,7,-64);
  }
  append_crc(b+14,600); append_crc(b+614,264);
}
static void test_crc_bch(void)
{
  uint8_t b[72],code[51]; const char *s="123456789";
  for (int j=0;j<9;++j) put(b,1+8*j,8,s[j]);
  assert(bds_b1c_crc24q(b,72)==0xcde703U);
  b[0]=2; assert(bds_b1c_crc24q(b,72)==UINT32_MAX);
  assert(bds_b1c_crc24q(NULL,1)==UINT32_MAX);
  for (int w=6;w<=8;w+=2) for (int v=0;v<(1<<w);++v) {
    int n=w==6?21:51; encode(v,w,code,n);
    assert(bds_b1c_bch_decode(code,n)==v);
    for (int j=0;j<n;++j) { code[j]^=1; assert(bds_b1c_bch_decode(code,n)==-1); code[j]^=1; }
  }
  assert(bds_b1c_bch_decode(NULL,21)==-1);
  assert(bds_b1c_bch_decode(b,22)==-1);
}
static void test_frames(void)
{
  uint8_t b[878],symbols[1800]; bds_b1c_nav_frame_t f;
  bds_b1c_ephemeris_t e;
  const double pi=3.1415926535898;
  for (int page=1;page<=4;++page) {
    payload(b,page); interleave(b,symbols);
    assert(!bds_b1c_decode_frame(symbols,1800,23,&f));
    assert(f.status==BDS_B1C_NAV_OK&&f.ephemeris.valid&&f.crc2_ok&&f.crc3_ok);
    assert(!memcmp(b,f.bits,878)); e=f.ephemeris;
    assert(e.IODC==0x301&&e.IODE==0xa5&&e.TOW==601272&&e.WN==8191);
    assert(e.deltaA==-65536&&e.M_0==-pi&&e.omega==ldexp(4294967295.,-32)*pi);
    assert(e.omega_0==-pi&&e.e==ldexp(8589934591.,-34));
    assert(e.C_rs==-32768&&e.C_rc==ldexp(8388607.,-8));
    assert(e.a_0==ldexp(-16777216.,-34)&&e.a_1==ldexp(-2097152.,-50)&&e.a_2==ldexp(-1024.,-66));
    assert(e.HS==3&&e.DIF==1&&e.SIF==1&&e.AIF==1); /* Decoded != healthy. */
    if (page==1) {
      assert(e.alpha1==127.875&&e.alpha2==-16&&e.alpha3==31.875);
      assert(e.delta_t_LS==-128&&e.DN==7&&e.delta_t_LSF==127&&e.t_ot==1048560);
      assert(isnan(e.A_0BGTO));
    } else if (page==3) {
      assert(e.GNSS_ID==7&&e.WN_0BGTO==8191&&e.t_0BGTO==1048560);
      assert(e.A_0BGTO==ldexp(-32768.,-35)&&isnan(e.alpha1)&&isnan(e.A_0UTC));
    } else assert(isnan(e.alpha1)&&isnan(e.A_0BGTO));
    for (int j=0;j<1800;++j) symbols[j]^=1;
    assert(!bds_b1c_decode_frame(symbols,1800,23,&f));
    assert(f.status==BDS_B1C_NAV_OK&&f.inverted&&!memcmp(b,f.bits,878));
  }
  payload(b,1); interleave(b,symbols);
  symbols[0]^=1; assert(!bds_b1c_decode_frame(symbols,1800,23,&f)&&f.status==BDS_B1C_NAV_BCH21);
  interleave(b,symbols); symbols[21]^=1;
  assert(!bds_b1c_decode_frame(symbols,1800,23,&f)&&f.status==BDS_B1C_NAV_BCH51);
  interleave(b,symbols); assert(!bds_b1c_decode_frame(symbols,1800,24,&f)&&f.status==BDS_B1C_NAV_PRN);
  b[14]^=1; interleave(b,symbols);
  assert(!bds_b1c_decode_frame(symbols,1800,23,&f)&&f.status==BDS_B1C_NAV_CRC2&&!f.ephemeris.valid);
  b[14]^=1; b[614]^=1; interleave(b,symbols);
  assert(!bds_b1c_decode_frame(symbols,1800,23,&f)&&f.status==BDS_B1C_NAV_CRC3&&!f.ephemeris.valid);
  assert(bds_b1c_decode_navigation(b,878,23,&e)==-2&&!e.valid);
  payload(b,1); put(b+14,14,8,168); append_crc(b+14,600); interleave(b,symbols);
  assert(!bds_b1c_decode_frame(symbols,1800,23,&f)&&f.status==BDS_B1C_NAV_FIELDS&&!f.ephemeris.valid);
  payload(b,1); b[20]=2; assert(bds_b1c_decode_navigation(b,878,23,&e)==-1&&!e.valid);
  assert(bds_b1c_decode_navigation(b,192,23,&e)==-1);
  symbols[0]=2; assert(bds_b1c_decode_frame(symbols,1800,23,&f)==-1);
  assert(bds_b1c_decode_frame(NULL,1800,23,&f)==-1);
  assert(bds_b1c_decode_frame(symbols,1799,23,&f)==-1);
}
struct collected { unsigned count; bds_b1c_nav_frame_t last; };
static void collect(const bds_b1c_nav_frame_t *f,void *v)
{ struct collected *c=v; ++c->count; c->last=*f; }
static void test_stream(void)
{
  uint8_t b[878],data[1800]; int8_t pilot[1800];
  bds_b1c_navigator_t *n=NULL; struct collected c={0};
  payload(b,1); interleave(b,data);
  assert(!bds_b1c_generate_secondary_code(23,pilot,1800));
  assert(bds_b1c_navigator_create(0,1000,&n)==-1&&!n);
  assert(bds_b1c_navigator_create(23,NAN,&n)==-1&&!n);
  assert(!bds_b1c_navigator_create(23,1000,&n));
  bds_b1c_tracking_epoch_t e={0};
  e.prn=23; e.integration_s=.01; e.sample_count=10; e.valid=1; e.state=BDS_B1C_LOCKED;
  uint64_t base=UINT64_C(9007199254740993);
  for (int fault=0;fault<=7;++fault) {
    bds_b1c_navigator_reset(n); c.count=0;
    for (int j=0;j<1800;++j) {
      e.epoch=(uint64_t)j; e.generation=3; e.absolute_sample=base+10*(uint64_t)j;
      e.valid=1; e.data_prompt.i=data[j]?10:-10; e.pilot_prompt.q=-pilot[j]*100;
      int expected=0;
      if (j==900) {
        if (fault==1) e.valid=0;
        if (fault==2) ++e.absolute_sample;
        if (fault==3) ++e.generation;
        if (fault==4) ++e.epoch;
        if (fault==5) e.pilot_prompt.q=-e.pilot_prompt.q;
        if (fault==6) { e.data_prompt.i=NAN; expected=-1; }
        if (fault==7) { e.sample_count=9; expected=-1; }
      } else e.sample_count=10;
      assert(bds_b1c_navigator_push(n,&e,collect,&c)==expected);
      if (j<1799) assert(!c.count);
    }
    if (!fault) {
      assert(c.count==1&&c.last.status==BDS_B1C_NAV_OK&&c.last.absolute_sample==base);
      assert(c.last.end_sample==base+18000&&c.last.generation==3&&c.last.secondary_correlation==-1800);
    } else assert(!c.count);
  }
  /* Partial final windows cannot emit; a complete fresh generation can. */
  bds_b1c_navigator_reset(n); c.count=0;
  for (int j=0;j<3600;++j) {
    int k=j%1800; e.epoch=(uint64_t)k; e.generation=(uint64_t)(j/1800);
    e.absolute_sample=(uint64_t)j*10; e.sample_count=10; e.valid=1;
    e.data_prompt.i=data[k]?1:-1; e.pilot_prompt.q=pilot[k];
    assert(!bds_b1c_navigator_push(n,&e,collect,&c));
  }
  assert(c.count==2&&c.last.generation==1&&c.last.first_epoch==0);
  e.absolute_sample=UINT64_MAX; assert(bds_b1c_navigator_push(n,&e,collect,&c)==-1);
  bds_b1c_navigator_destroy(n);
}
int main(void)
{ test_crc_bch(); test_frames(); test_stream(); puts("B1C navigation tests passed"); return 0; }

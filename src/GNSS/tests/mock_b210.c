/* Test-only transport; never linked into the actual bds-b1c-live target. */
#include "uhd_b210.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
struct b1_radio {double fs;unsigned blocks;int mode;};
int b1_radio_open(const b1_radio_options_t* o,b1_radio_info_t* info,b1_radio_t** out,const volatile sig_atomic_t* stop,char* err,size_t size){
  (void)stop;
  if(strstr(o->args,"missing")){snprintf(err,size,"No matching serial");return -1;}
  b1_radio_t* r=calloc(1,sizeof(*r));if(!r)return -1;r->fs=o->sample_rate;r->mode=strstr(o->args,"backward")?1:0;
  memset(info,0,sizeof(*info));snprintf(info->address,sizeof(info->address),"test-only-mock");snprintf(info->antenna,sizeof(info->antenna),"RX2");
  info->sample_rate=o->sample_rate;info->frequency=o->frequency;info->reference_locked=info->pps_detected=1;*out=r;return 0;
}
int b1_radio_start(b1_radio_t* r){(void)r;return 0;}
int b1_radio_receive(b1_radio_t* r,bds5g_cf32_t* iq,size_t n,b1_radio_packet_t* p){
  usleep(20000);memset(iq,0,n*sizeof(*iq));memset(p,0,sizeof(*p));++r->blocks;
  p->tick=UINT64_C(9007199254741115)+(uint64_t)r->blocks*n;
  if(r->blocks==3){p->error=8;return 0;}
  if(r->mode&&r->blocks==5)p->tick-=10*n;
  p->count=n;return 0;
}
int b1_radio_clock(b1_radio_t* r,int* ref,int* pps){(void)r;*ref=*pps=1;return 0;}
const char* b1_radio_error(b1_radio_t* r){(void)r;return "mock transport error";}
void b1_radio_stop(b1_radio_t* r){(void)r;}
void b1_radio_close(b1_radio_t* r){free(r);}

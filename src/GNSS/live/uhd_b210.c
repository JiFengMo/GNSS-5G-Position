/* UHD C boundary keeps Boost/C++ UHD headers out of the DSP/UI build. */
#include "uhd_b210.h"
#include <uhd.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct b1_radio {
  uhd_usrp_handle usrp;uhd_rx_streamer_handle rx;uhd_rx_metadata_handle metadata;
  uhd_sensor_value_handle sensor;
  double fs,last_pps_frac,last_pps_change;int64_t last_pps_full;
  int started,external_pps,external_reference,has_reference;
};
// Errors belong to the calling RX/control thread.
static _Thread_local char radio_error[1024];
static double monotonic_s(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int checked(b1_radio_t* r,uhd_error e){
  if(e==UHD_ERROR_NONE)return 0;
  snprintf(radio_error,sizeof(radio_error),"UHD error %d",(int)e);
  if(r->usrp)uhd_usrp_last_error(r->usrp,radio_error,sizeof(radio_error));
  return -1;
}
#define CALL(x) do {if(checked(r,(x)))goto fail;}while(0)
int b1_radio_open(const b1_radio_options_t* o,b1_radio_info_t* info,b1_radio_t** out,const volatile sig_atomic_t* stop,char* error,size_t error_size){
  b1_radio_t* r=calloc(1,sizeof(*r));uhd_string_vector_handle devices=NULL,sensors=NULL;
  uhd_usrp_rx_info_t rxinfo;int have_info=0;size_t count=0;*out=NULL;memset(info,0,sizeof(*info));info->reference_locked=-1;
  if(!r){snprintf(error,error_size,"Cannot allocate UHD receiver");return -1;}
  CALL(uhd_string_vector_make(&devices));CALL(uhd_usrp_find(o->args,&devices));CALL(uhd_string_vector_size(devices,&count));
  if(!count){snprintf(radio_error,sizeof(radio_error),"No connected B210 matches device_args/serial; no fallback device selected");goto fail;}
  CALL(uhd_string_vector_at(devices,0,info->address,sizeof(info->address)));
  /* Keep transport hints, while pinning the discovered first/matching radio. */
  char make_args[2048];
  if(snprintf(make_args,sizeof(make_args),"%s,%s",o->args,info->address)>=(int)sizeof(make_args)){snprintf(radio_error,sizeof(radio_error),"UHD device arguments too long");goto fail;}
  CALL(uhd_usrp_make(&r->usrp,make_args));CALL(uhd_usrp_get_rx_info(r->usrp,0,&rxinfo));have_info=1;
  snprintf(info->board,sizeof(info->board),"%s",rxinfo.mboard_id);
  if(!strstr(info->board,"B210")){snprintf(radio_error,sizeof(radio_error),"Selected radio is not a B210");goto fail;}
  CALL(uhd_usrp_set_clock_source(r->usrp,o->clock_source,0));CALL(uhd_usrp_set_time_source(r->usrp,o->time_source,0));
  if(o->master_clock>0)CALL(uhd_usrp_set_master_clock_rate(r->usrp,o->master_clock,0));
  CALL(uhd_usrp_set_rx_rate(r->usrp,o->sample_rate,0));
  uhd_tune_request_t tune={.target_freq=o->frequency,.rf_freq_policy=UHD_TUNE_REQUEST_POLICY_AUTO,.dsp_freq_policy=UHD_TUNE_REQUEST_POLICY_AUTO,.args=""};uhd_tune_result_t result;
  CALL(uhd_usrp_set_rx_freq(r->usrp,&tune,0,&result));CALL(uhd_usrp_set_rx_bandwidth(r->usrp,o->bandwidth,0));
  CALL(uhd_usrp_set_rx_gain(r->usrp,o->gain,0,""));CALL(uhd_usrp_set_rx_antenna(r->usrp,o->antenna,0));
  CALL(uhd_usrp_get_rx_rate(r->usrp,0,&info->sample_rate));r->fs=info->sample_rate;
  CALL(uhd_usrp_get_rx_freq(r->usrp,0,&info->frequency));CALL(uhd_usrp_get_rx_bandwidth(r->usrp,0,&info->bandwidth));
  CALL(uhd_usrp_get_rx_gain(r->usrp,0,"",&info->gain));CALL(uhd_usrp_get_rx_antenna(r->usrp,0,info->antenna,sizeof(info->antenna)));
  CALL(uhd_string_vector_make(&sensors));CALL(uhd_usrp_get_mboard_sensor_names(r->usrp,0,&sensors));CALL(uhd_string_vector_size(sensors,&count));
  for(size_t j=0;j<count;++j){char name[128];CALL(uhd_string_vector_at(sensors,j,name,sizeof(name)));if(!strcmp(name,"ref_locked"))r->has_reference=1;}
  r->external_reference=!strcmp(o->clock_source,"external");r->external_pps=!strcmp(o->time_source,"external");
  if(r->has_reference){
    CALL(uhd_sensor_value_make(&r->sensor));bool locked=false;
    for(int j=0;j<30&&!*stop;++j){CALL(uhd_usrp_get_mboard_sensor(r->usrp,"ref_locked",0,&r->sensor));CALL(uhd_sensor_value_to_bool(r->sensor,&locked));if(locked)break;usleep(100000);}
    info->reference_locked=locked;
    if(r->external_reference&&!locked){snprintf(radio_error,sizeof(radio_error),"External 10 MHz reference did not lock");goto fail;}
  }
  if(r->external_pps){
    int64_t before,now;double before_frac,now_frac;
    CALL(uhd_usrp_get_time_last_pps(r->usrp,0,&before,&before_frac));
    for(int j=0;j<25&&!*stop;++j){usleep(100000);CALL(uhd_usrp_get_time_last_pps(r->usrp,0,&now,&now_frac));if(now!=before||now_frac!=before_frac){info->pps_detected=1;break;}}
    if(!info->pps_detected){snprintf(radio_error,sizeof(radio_error),"External PPS not detected");goto fail;}
    CALL(uhd_usrp_set_time_unknown_pps(r->usrp,0,0));
  }else CALL(uhd_usrp_set_time_now(r->usrp,0,0,0));
  if(*stop){snprintf(radio_error,sizeof(radio_error),"Receiver startup interrupted");goto fail;}
  CALL(uhd_usrp_get_time_last_pps(r->usrp,0,&r->last_pps_full,&r->last_pps_frac));r->last_pps_change=monotonic_s();
  CALL(uhd_rx_streamer_make(&r->rx));CALL(uhd_rx_metadata_make(&r->metadata));
  size_t channel=0;uhd_stream_args_t stream={.cpu_format="fc32",.otw_format="sc16",.args="",.channel_list=&channel,.n_channels=1};
  CALL(uhd_usrp_get_rx_stream(r->usrp,&stream,r->rx));
  uhd_usrp_rx_info_free(&rxinfo);uhd_string_vector_free(&devices);uhd_string_vector_free(&sensors);*out=r;return 0;
fail:
  snprintf(error,error_size,"%s",radio_error);if(have_info)uhd_usrp_rx_info_free(&rxinfo);
  if(devices)uhd_string_vector_free(&devices);
  if(sensors)uhd_string_vector_free(&sensors);
  b1_radio_close(r);return -1;
}
#undef CALL
int b1_radio_start(b1_radio_t* r){
  int64_t full;double frac;if(checked(r,uhd_usrp_get_time_now(r->usrp,0,&full,&frac)))return -1;
  frac+=.2;if(frac>=1){++full;frac-=1;}
  uhd_stream_cmd_t cmd={.stream_mode=UHD_STREAM_MODE_START_CONTINUOUS,.stream_now=false,.time_spec_full_secs=full,.time_spec_frac_secs=frac};
  if(checked(r,uhd_rx_streamer_issue_stream_cmd(r->rx,&cmd)))return -1;
  r->started=1;return 0;
}
int b1_radio_receive(b1_radio_t* r,bds5g_cf32_t* iq,size_t n,b1_radio_packet_t* p){
  void* buffer=iq;memset(p,0,sizeof(*p));
  if(checked(r,uhd_rx_streamer_recv(r->rx,&buffer,n,&r->metadata,.3,false,&p->count)))return -1;
  uhd_rx_metadata_error_code_t code;
  if(checked(r,uhd_rx_metadata_error_code(r->metadata,&code)))return -1;
  p->error=(int)code;
  if(code!=UHD_RX_METADATA_ERROR_CODE_NONE||!p->count)return 0;
  bool timed=false;int64_t full;double frac;
  if(checked(r,uhd_rx_metadata_has_time_spec(r->metadata,&timed))||!timed){snprintf(radio_error,sizeof(radio_error),"RX block lacks hardware timestamp");return -1;}
  if(checked(r,uhd_rx_metadata_time_spec(r->metadata,&full,&frac)))return -1;
  long double tick=(long double)full*r->fs+(long double)frac*r->fs;
  if(!isfinite(tick)||tick<0||tick>(long double)UINT64_MAX-.5L){snprintf(radio_error,sizeof(radio_error),"RX timestamp out of range");return -1;}
  p->tick=(uint64_t)floorl(tick+.5L);return 0;
}
int b1_radio_clock(b1_radio_t* r,int* reference,int* pps){
  *reference=-1;*pps=0;
  if(r->has_reference){bool value=false;if(checked(r,uhd_usrp_get_mboard_sensor(r->usrp,"ref_locked",0,&r->sensor))||checked(r,uhd_sensor_value_to_bool(r->sensor,&value)))return -1;*reference=value;}
  if(r->external_pps){int64_t full;double frac;if(checked(r,uhd_usrp_get_time_last_pps(r->usrp,0,&full,&frac)))return -1;
    if(full!=r->last_pps_full||frac!=r->last_pps_frac){r->last_pps_full=full;r->last_pps_frac=frac;r->last_pps_change=monotonic_s();}
    *pps=monotonic_s()-r->last_pps_change<2.5;
  }
  return 0;
}
const char* b1_radio_error(b1_radio_t* r){(void)r;return radio_error;}
void b1_radio_stop(b1_radio_t* r){if(r&&r->started){uhd_stream_cmd_t cmd={.stream_mode=UHD_STREAM_MODE_STOP_CONTINUOUS,.stream_now=true};uhd_rx_streamer_issue_stream_cmd(r->rx,&cmd);r->started=0;}}
void b1_radio_close(b1_radio_t* r){if(!r)return;b1_radio_stop(r);if(r->sensor)uhd_sensor_value_free(&r->sensor);if(r->metadata)uhd_rx_metadata_free(&r->metadata);if(r->rx)uhd_rx_streamer_free(&r->rx);if(r->usrp)uhd_usrp_free(&r->usrp);free(r);}

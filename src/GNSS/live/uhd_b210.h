#pragma once
#include "bds5g/types.h"
#include <signal.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct b1_radio b1_radio_t;
typedef struct {
  const char *args,*clock_source,*time_source,*antenna;
  double sample_rate,frequency,bandwidth,gain,master_clock;
} b1_radio_options_t;
typedef struct {
  char address[1024],board[128],antenna[128];
  double sample_rate,frequency,bandwidth,gain;
  int reference_locked,pps_detected; /* -1 reference sensor unavailable */
} b1_radio_info_t;
typedef struct {size_t count;uint64_t tick;int error;} b1_radio_packet_t;
int b1_radio_open(const b1_radio_options_t*,b1_radio_info_t*,b1_radio_t**,const volatile sig_atomic_t*,char*,size_t);
int b1_radio_start(b1_radio_t*);
int b1_radio_receive(b1_radio_t*,bds5g_cf32_t*,size_t,b1_radio_packet_t*);
int b1_radio_clock(b1_radio_t*,int* reference_locked,int* pps_detected);
const char* b1_radio_error(b1_radio_t*);
void b1_radio_stop(b1_radio_t*);
void b1_radio_close(b1_radio_t*);
#ifdef __cplusplus
}
#endif

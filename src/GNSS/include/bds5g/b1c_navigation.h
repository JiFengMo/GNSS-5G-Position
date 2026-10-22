#pragma once
#include "bds5g/b1c_tracking.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BDS_B1C_FRAME_SYMBOLS 1800U
#define BDS_B1C_NAV_BITS 878U
typedef enum {
  BDS_B1C_NAV_OK = 0, BDS_B1C_NAV_BCH21, BDS_B1C_NAV_BCH51,
  BDS_B1C_NAV_PRN, BDS_B1C_NAV_CRC2, BDS_B1C_NAV_CRC3, BDS_B1C_NAV_FIELDS
} bds_b1c_nav_status_t;
typedef struct {
  int prn;
  uint64_t generation, first_epoch, absolute_sample, end_sample;
  double sample_time_s; /* Record-relative reception time; NOT BDT or UTC. */
  int secondary_correlation; /* +1800 or -1800; exact MATLAB gate. */
  int inverted; /* Data polarity reversed to pass SF1 BCH. */
  uint8_t crc2_ok, crc3_ok;
  bds_b1c_nav_status_t status;
  uint8_t bits[BDS_B1C_NAV_BITS];
  bds_b1c_ephemeris_t ephemeris;
} bds_b1c_nav_frame_t;
/* CRC24Q: MSB first, poly 0x1864CFB, init/xorout 0, no reflection.
 * Complete message including CRC has remainder zero.
 * UINT32_MAX on invalid input. */
uint32_t bds_b1c_crc24q(const uint8_t *bits, size_t count);
/* Exact BCH21/51 codeword check, MATLAB thresholds 20/50. Returns decoded
 * integer, or -1. No bit-error correction claimed on this path. */
int bds_b1c_bch_decode(const uint8_t *symbols, size_t count);
/* 1800 hard data symbols (positive I_P -> 1). Systematic LDPC information
 * WITHOUT LDPC correction, like CU-SDR BCNAV1decoding.m. Both CRCs required.
 * No secondary/tracking gate here; use navigator_push for that.
 * -1 invalid input; otherwise 0, result in frame.status. */
int bds_b1c_decode_frame(const uint8_t *symbols, size_t count, int prn,
                         bds_b1c_nav_frame_t *frame);
const char *bds_b1c_nav_status_name(bds_b1c_nav_status_t status);
typedef struct bds_b1c_navigator bds_b1c_navigator_t;
typedef void (*bds_b1c_nav_callback_t)(const bds_b1c_nav_frame_t *, void *);
int bds_b1c_navigator_create(int prn, double sample_rate_hz,
                             bds_b1c_navigator_t **navigator);
void bds_b1c_navigator_destroy(bds_b1c_navigator_t *navigator);
void bds_b1c_navigator_reset(bds_b1c_navigator_t *navigator);
/* One complete narrowband epoch at a time; synchronous callback, no reentry.
 * Requires valid+LOCKED, consecutive samples AND epoch IDs in one generation.
 * Invalid epochs/gaps/resets clear pending 1800 symbols; partial EOF emits
 * nothing. Nonfinite/invalid arguments return -1 and clear pending data.
 * Uses pilot Q and data I signs. Callback for each exact secondary match,
 * including BCH/CRC failures. */
int bds_b1c_navigator_push(bds_b1c_navigator_t *navigator,
                           const bds_b1c_tracking_epoch_t *epoch,
                           bds_b1c_nav_callback_t callback, void *context);
#ifdef __cplusplus
}
#endif

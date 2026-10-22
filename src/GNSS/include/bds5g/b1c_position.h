#pragma once
#include "bds5g/b1c_navigation.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BDS_B1C_SPEED_OF_LIGHT 299792458.0
#define BDS_B1C_MAX_SATELLITES 63U
typedef struct { int week; double tow_s; } bds_b1c_time_t;
typedef struct {
  double ecef_m[3];
  double clock_correction_s; /* Polynomial - T_GDB1Cp + relativity; joint tracking. */
  double relativistic_s, orbit_age_s, clock_age_s;
} bds_b1c_satellite_t;
/* Frame time + whole code cycles + fractional code phase at ONE common
 * reception sample. Requires the same generation, valid locked full epoch.
 * Caller must also verify continuity between anchor and epoch. */
int bds_b1c_transmit_time(const bds_b1c_nav_frame_t *anchor,
                          const bds_b1c_tracking_epoch_t *epoch,
                          uint64_t reception_sample, double sample_rate_hz,
                          bds_b1c_time_t *time);
/* CU-SDR satpos: MEO/IGSO, clock, relativity, pilot group delay.
 * -1 invalid, -2 stale ephemeris/clock, -3 Kepler/numerical failure,
 * -4 unsupported GEO (outside this stage's validated orbit models).
 * Ages use ephemeris frame week/TOW, so old weeks cannot masquerade as fresh.
 * max_age_s is an explicit application limit, not an ICD fit guarantee. */
int bds_b1c_satellite_state(const bds_b1c_ephemeris_t *ephemeris,
                            bds_b1c_time_t transmit_time, double max_age_s,
                            bds_b1c_satellite_t *state);
typedef struct {
  int prn;
  bds_b1c_satellite_t satellite;
  double pseudorange_m; /* Raw code pseudorange; satellite clock not yet applied. */
} bds_b1c_spp_observation_t;
typedef struct {
  double ecef_m[3], clock_correction_m;
  double latitude_deg, longitude_deg, height_m;
  double gdop,pdop,hdop,vdop,tdop; /* HDOP/VDOP in local ENU. */
  double residual_rms_m;
  double elevation_deg[63],azimuth_deg[63],residual_m[63],troposphere_m[63];
  unsigned satellites,iterations;
  uint8_t valid;
} bds_b1c_spp_solution_t;
/* Equal-weight 4-state least squares, ten iterations from Earth centre,
 * Earth-rotation correction and optional original sea-level tropo model.
 * Householder QR rejects singular/ill-conditioned geometry; convergence and
 * finite outputs required. -1 invalid, -2 <4 sats, -3 geometry, -4 no fix.
 * No ionosphere correction, RAIM or carrier-phase positioning in this path. */
int bds_b1c_spp_solve(const bds_b1c_spp_observation_t *observations,
                      size_t count, int use_troposphere,
                      bds_b1c_spp_solution_t *solution);
int bds_b1c_ecef_to_geodetic(const double xyz[3],double *latitude_deg,
                             double *longitude_deg,double *height_m);
int bds_b1c_azimuth_elevation(const double receiver[3],const double satellite[3],
                              double *azimuth_deg,double *elevation_deg);
#ifdef __cplusplus
}
#endif

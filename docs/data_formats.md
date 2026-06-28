# Data formats

## Complex IQ input

`bds-b1c-receiver` reads binary interleaved complex samples using the native
layout below:

```c
typedef struct {
  float i;
  float q;
} bds5g_cf32_t;
```

The sample rate and PRN are supplied on the command line. Frequency values use
Hz and time values use seconds or nanoseconds as indicated by each option.

## Common observation CSV

All positioning inputs use one row per satellite or NR anchor observation:

```text
timestamp_ns,source,source_id,source_x_m,source_y_m,source_z_m,pseudorange_m,sigma_m,cn0_db_hz,valid
```

Source identifiers:

| Value | System |
|---:|---|
| 0 | BeiDou B1C |
| 1 | NR SSB |
| 2 | NR PRS |

Coordinates are ECEF meters. `sigma_m` is the one-sigma measurement uncertainty
used by the WLS weight matrix. Invalid rows remain traceable in a file but are
excluded from the solution.

## Position solution JSON

`bds5g-positioning` writes:

```json
{
  "ecef_m": [0.0, 0.0, 0.0],
  "bds_clock_bias_m": 0.0,
  "nr_clock_bias_m": 0.0,
  "residual_rms_m": 0.0,
  "used_observations": 0,
  "converged": true
}
```


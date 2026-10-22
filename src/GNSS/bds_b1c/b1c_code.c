#include "bds5g/b1c.h"

#include <math.h>
#include <string.h>

/* BDS-SIS-ICD-B1C-1.0 (2017-12), Tables 5-2, 5-3 and 5-4.
 * Pairs are (Weil phase difference w, one-based truncation p).
 * Cross-checked against CU-SDR BDS/B1C; see docs/offline_acquisition.md. */
static const uint16_t data_parameters[63][2] = {
  {2678, 699},
  {4802, 694},
  {958, 7318},
  {859, 2127},
  {3843, 715},
  {2232, 6682},
  {124, 7850},
  {4352, 5495},
  {1816, 1162},
  {1126, 7682},
  {1860, 6792},
  {4800, 9973},
  {2267, 6596},
  {424, 2092},
  {4192, 19},
  {4333, 10151},
  {2656, 6297},
  {4148, 5766},
  {243, 2359},
  {1330, 7136},
  {1593, 1706},
  {1470, 2128},
  {882, 6827},
  {3202, 693},
  {5095, 9729},
  {2546, 1620},
  {1733, 6805},
  {4795, 534},
  {4577, 712},
  {1627, 1929},
  {3638, 5355},
  {2553, 6139},
  {3646, 6339},
  {1087, 1470},
  {1843, 6867},
  {216, 7851},
  {2245, 1162},
  {726, 7659},
  {1966, 1156},
  {670, 2672},
  {4130, 6043},
  {53, 2862},
  {4830, 180},
  {182, 2663},
  {2181, 6940},
  {2006, 1645},
  {1080, 1582},
  {2288, 951},
  {2027, 6878},
  {271, 7701},
  {915, 1823},
  {497, 2391},
  {139, 2606},
  {3693, 822},
  {2054, 6403},
  {4342, 239},
  {3342, 442},
  {2592, 6769},
  {1007, 2560},
  {310, 2502},
  {4203, 5072},
  {455, 7268},
  {4318, 341}
};

static const uint16_t pilot_parameters[63][2] = {
  {796, 7575},
  {156, 2369},
  {4198, 5688},
  {3941, 539},
  {1374, 2270},
  {1338, 7306},
  {1833, 6457},
  {2521, 6254},
  {3175, 5644},
  {168, 7119},
  {2715, 1402},
  {4408, 5557},
  {3160, 5764},
  {2796, 1073},
  {459, 7001},
  {3594, 5910},
  {4813, 10060},
  {586, 2710},
  {1428, 1546},
  {2371, 6887},
  {2285, 1883},
  {3377, 5613},
  {4965, 5062},
  {3779, 1038},
  {4547, 10170},
  {1646, 6484},
  {1430, 1718},
  {607, 2535},
  {2118, 1158},
  {4709, 526},
  {1149, 7331},
  {3283, 5844},
  {2473, 6423},
  {1006, 6968},
  {3670, 1280},
  {1817, 1838},
  {771, 1989},
  {2173, 6468},
  {740, 2091},
  {1433, 1581},
  {2458, 1453},
  {3459, 6252},
  {2155, 7122},
  {1205, 7711},
  {413, 7216},
  {874, 2113},
  {2463, 1095},
  {1106, 1628},
  {1590, 1713},
  {3873, 6102},
  {4026, 6123},
  {4272, 6070},
  {3556, 1115},
  {128, 8047},
  {1200, 6795},
  {130, 2575},
  {4494, 53},
  {1871, 1729},
  {3073, 6388},
  {4386, 682},
  {4098, 5565},
  {1923, 7160},
  {1176, 2277}
};

static const uint16_t secondary_parameters[63][2] = {
  {269, 1889},
  {1448, 1268},
  {1028, 1593},
  {1324, 1186},
  {822, 1239},
  {5, 1930},
  {155, 176},
  {458, 1696},
  {310, 26},
  {959, 1344},
  {1238, 1271},
  {1180, 1182},
  {1288, 1381},
  {334, 1604},
  {885, 1333},
  {1362, 1185},
  {181, 31},
  {1648, 704},
  {838, 1190},
  {313, 1646},
  {750, 1385},
  {225, 113},
  {1477, 860},
  {309, 1656},
  {108, 1921},
  {1457, 1173},
  {149, 1928},
  {322, 57},
  {271, 150},
  {576, 1214},
  {1103, 1148},
  {450, 1458},
  {399, 1519},
  {241, 1635},
  {1045, 1257},
  {164, 1687},
  {513, 1382},
  {687, 1514},
  {422, 1},
  {303, 1583},
  {324, 1806},
  {495, 1664},
  {725, 1338},
  {780, 1111},
  {367, 1706},
  {882, 1543},
  {631, 1813},
  {37, 228},
  {647, 2871},
  {1043, 2884},
  {24, 1823},
  {120, 75},
  {134, 11},
  {136, 63},
  {158, 1937},
  {214, 22},
  {335, 1768},
  {340, 1526},
  {661, 1402},
  {889, 1445},
  {929, 1680},
  {1002, 1290},
  {1149, 1245}
};

static int generate(int prn, const uint16_t parameters[63][2], unsigned prime,
                    size_t length, int8_t *chips, size_t count)
{
  uint8_t legendre[10243];
  if (!chips || count < length || prn < 1 || prn > 63) return -1;
  memset(legendre, 0, sizeof(legendre));
  /* The set of nonzero quadratic residues gives ICD L(k); L(0) = 0. */
  for (unsigned x = 1; x <= (prime - 1U) / 2U; ++x)
    legendre[(x * x) % prime] = 1;
  const unsigned w = parameters[prn - 1][0];
  const unsigned p = parameters[prn - 1][1];
  for (size_t n = 0; n < length; ++n) {
    const unsigned k = ((unsigned)n + p - 1U) % prime;
    chips[n] = (int8_t)(1 - 2 * (legendre[k] ^ legendre[(k + w) % prime]));
  }
  return 0;
}

int bds_b1c_generate_code(int prn, bds_b1c_component_t component,
                          int8_t *chips, size_t count)
{
  if (component != BDS_B1C_DATA && component != BDS_B1C_PILOT) return -1;
  return generate(prn, component == BDS_B1C_DATA ? data_parameters : pilot_parameters,
                  10243U, BDS_B1C_PRIMARY_CODE_LENGTH, chips, count);
}

int bds_b1c_generate_primary_code(int prn, int8_t *chips, size_t count)
{
  return bds_b1c_generate_code(prn, BDS_B1C_DATA, chips, count);
}

int bds_b1c_generate_secondary_code(int prn, int8_t *chips, size_t count)
{
  return generate(prn, secondary_parameters, 3607U,
                  BDS_B1C_SECONDARY_CODE_LENGTH, chips, count);
}

int bds_b1c_sample_boc11(int prn, bds_b1c_component_t component,
                         double fs, int8_t *samples, size_t count)
{
  int8_t chips[BDS_B1C_PRIMARY_CODE_LENGTH];
  if (!samples || !isfinite(fs) || fs < 2.0 * BDS_B1C_CHIP_RATE_HZ ||
      fs > 100000000.0) return -1;
  const size_t length = (size_t)llround(fs * 0.01);
  if (count < length || bds_b1c_generate_code(prn, component, chips, sizeof(chips))) return -1;
  const double ts = 1.0 / fs;
  const double tc = 1.0 / BDS_B1C_CHIP_RATE_HZ / 2.0;
  for (size_t n = 0; n < length; ++n) {
    size_t index = (size_t)ceil((ts * (double)(n + 1U)) / tc);
    if (n == 0) index = 1;
    if (n == length - 1U) index = 2U * BDS_B1C_PRIMARY_CODE_LENGTH;
    if (index < 1U || index > 2U * BDS_B1C_PRIMARY_CODE_LENGTH) return -1;
    --index;
    samples[n] = (int8_t)((index & 1U) ? chips[index / 2U] : -chips[index / 2U]);
  }
  return 0;
}

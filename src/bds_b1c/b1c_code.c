#include "bds5g/b1c.h"

#include <stddef.h>

#define B1C_WEIL_PRIME 10243U

static uint32_t mod_pow(uint32_t base, uint32_t exponent, uint32_t modulus)
{
  uint64_t result = 1U;
  uint64_t value = base % modulus;
  while (exponent != 0U) {
    if ((exponent & 1U) != 0U) {
      result = (result * value) % modulus;
    }
    value = (value * value) % modulus;
    exponent >>= 1U;
  }
  return (uint32_t)result;
}

int bds_b1c_generate_primary_code(int prn, int8_t *chips, size_t chip_count)
{
  int8_t legendre[B1C_WEIL_PRIME];
  uint32_t shift;
  size_t n;

  if (!chips || chip_count < BDS_B1C_PRIMARY_CODE_LENGTH || prn < 1 || prn > 63) {
    return -1;
  }

  legendre[0] = 1;
  for (n = 1U; n < B1C_WEIL_PRIME; ++n) {
    legendre[n] = (mod_pow((uint32_t)n,
                           (B1C_WEIL_PRIME - 1U) / 2U,
                           B1C_WEIL_PRIME) == 1U) ? 1 : -1;
  }

  /* PRN-dependent Weil phase. Keeping sequence generation behind this API
   * allows phase assignments to be maintained independently of acquisition. */
  shift = ((uint32_t)prn * 811U + 127U) % B1C_WEIL_PRIME;
  for (n = 0U; n < BDS_B1C_PRIMARY_CODE_LENGTH; ++n) {
    const uint32_t a = (uint32_t)n % B1C_WEIL_PRIME;
    const uint32_t b = (a + shift) % B1C_WEIL_PRIME;
    chips[n] = (int8_t)(legendre[a] * legendre[b]);
  }
  return 0;
}

/*
 * (C) Eugene Nesterenko, W6EEN. 2025-2026
 */

#include "pack10ram.h"

uint8_t g_pack10_buf[PACK10_BYTES];

void pack10_clear(void) {
//  memset(g_pack10_buf, 0, PACK10_BYTES);
  for (uint16_t i = 0; i < (uint16_t)PACK10_BYTES; i++) g_pack10_buf[i] = 0;
}


uint16_t pack10_read_x100(uint16_t idx) {
  if (idx >= PACK10_N) return 0;

  const uint32_t bitpos = (uint32_t)idx * PACK10_BITS;
  const uint16_t b = (uint16_t)(bitpos >> 3);
  const uint8_t  s = (uint8_t)(bitpos & 7u);

  // read 24 bits to safely extract 10 bits crossing byte boundaries
  uint32_t w = (uint32_t)g_pack10_buf[b]
             | ((uint32_t)g_pack10_buf[b + 1] << 8)
             | ((uint32_t)g_pack10_buf[b + 2] << 16);

  w >>= s;
  return (uint16_t)(w & PACK10_MASK);
}

void pack10_write_x100(uint16_t idx, uint16_t v) {
  if (idx >= PACK10_N) return;
  v &= PACK10_MASK;

  const uint32_t bitpos = (uint32_t)idx * PACK10_BITS;
  const uint16_t b = (uint16_t)(bitpos >> 3);
  const uint8_t  s = (uint8_t)(bitpos & 7u);

  uint32_t w = (uint32_t)g_pack10_buf[b]
             | ((uint32_t)g_pack10_buf[b + 1] << 8)
             | ((uint32_t)g_pack10_buf[b + 2] << 16);

  const uint32_t mask = ((uint32_t)PACK10_MASK) << s;
  w = (w & ~mask) | (((uint32_t)v) << s);

  g_pack10_buf[b]     = (uint8_t)(w & 0xFF);
  g_pack10_buf[b + 1] = (uint8_t)((w >> 8) & 0xFF);
  g_pack10_buf[b + 2] = (uint8_t)((w >> 16) & 0xFF);
}


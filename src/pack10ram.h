#pragma once
#include <Arduino.h>

// 10-bit packed array (0..1023) stored in RAM.
// Intended meaning in aa-code:
//   0      = invalid
//   100..1000 = 1.00..10.00 (centi-SWR)
//   1001..1023 = overflow/saturated

#ifndef PACK10_N
  #define PACK10_N 300   // must match GRAPH_W
#endif

#define PACK10_BITS 10u
#define PACK10_MASK 0x03FFu

// Bytes needed for N 10-bit entries + 2 pad bytes for safe 3-byte access
#define PACK10_BYTES (((PACK10_N * PACK10_BITS) + 7u) / 8u + 2u)

extern uint8_t g_pack10_buf[PACK10_BYTES];

void pack10_clear(void);

uint16_t pack10_read_x100(uint16_t idx);

void pack10_write_x100(uint16_t idx, uint16_t v);


#pragma once
#include <stdint.h>

/*
  Compatibility wrapper for Pico-SWR.
  Backend: patched tiny5351 (C API).
  

  Required tiny5351 functions (must exist in tiny5351.h):
    void si5351_set_xtal_freq(uint32_t xtal_hz);
    void si5351_set_freqs(uint32_t f0, uint32_t f1, uint32_t f2, uint8_t enable_mask);
*/

#ifdef __cplusplus
extern "C" {
#endif

#define  TINY5351_MINMAL 1

#include "tiny5351.h"   // must declare the patched functions above

#ifdef __cplusplus
}
#endif

// Minimal constants to keep the Pico-SWR call sites compiling.
// Values are accepted but ignored unless you extend tiny5351 to use them.
#ifndef SI5351_CLK_DRIVE_2MA
#define SI5351_CLK_DRIVE_2MA 0
#endif
#ifndef SI5351_CLK_DRIVE_4MA
#define SI5351_CLK_DRIVE_4MA 1
#endif
#ifndef SI5351_CLK_DRIVE_6MA
#define SI5351_CLK_DRIVE_6MA 2
#endif
#ifndef SI5351_CLK_DRIVE_8MA
#define SI5351_CLK_DRIVE_8MA 3
#endif

class Si5351Compat {
public:
  // Pico-SWR writes this in setup(); tiny5351 backend doesn't use it by default.
  uint32_t VCOFreq_Max = 800000000UL;

  Si5351Compat();

  // Kept for source compatibility. Drive args are stored but not used unless extended.
  void setup(uint8_t drive0, uint8_t drive1, uint8_t drive2);

  // Match the old signature used by your sketch (2nd arg defaulted).
  void set_xtal_freq(uint32_t xtal_hz, uint8_t reset_pll = 1);

  // Match the old signature: set all three at once.
  bool set_freq(uint32_t f0, uint32_t f1, uint32_t f2);

  // Pico-SWR calls is_freq_ok(0) and is_freq_ok(1).
  // tiny5351 doesn't validate; we implement a conservative check.
  bool is_freq_ok(uint8_t clk_num) const;

  // Compatibility: put outputs into a stable calibration mode.
  // This implementation outputs XTAL on CLK0 only (others disabled).
  void out_calibrate_freq();

private:
  uint32_t _xtal_hz;
  uint8_t  _drive0, _drive1, _drive2;

  // Cache last requested freqs so is_freq_ok() can reason about them.
  uint32_t _last_f0, _last_f1, _last_f2;

  // Backend model: tiny5351 uses PLLA = 30 * XTAL (per upstream).
  // If you later change tiny5351 to dynamic PLL, update this logic.
  bool freq_ok_for_backend(uint32_t freq_hz) const;
};


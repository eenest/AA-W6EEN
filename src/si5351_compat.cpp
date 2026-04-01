#include "si5351_compat.h"

Si5351Compat::Si5351Compat()
: _xtal_hz(25000000UL),
  _drive0(SI5351_CLK_DRIVE_2MA),
  _drive1(SI5351_CLK_DRIVE_2MA),
  _drive2(SI5351_CLK_DRIVE_2MA),
  _last_f0(0), _last_f1(0), _last_f2(0)
{
}

void Si5351Compat::setup(uint8_t drive0, uint8_t drive1, uint8_t drive2)
{
  // tiny5351 does not expose drive strength; store for possible future use.
  _drive0 = (uint8_t)(drive0 & 0x03);
  _drive1 = (uint8_t)(drive1 & 0x03);
  _drive2 = (uint8_t)(drive2 & 0x03);

  si5351_set_drives(_drive0, _drive1, _drive2);

}

void Si5351Compat::set_xtal_freq(uint32_t xtal_hz, uint8_t /*reset_pll*/)
{
  if (xtal_hz == 0) return;
  _xtal_hz = xtal_hz;

  // patched tiny5351 runtime setter
  si5351_set_xtal_freq(_xtal_hz);
}

bool Si5351Compat::set_freq(uint32_t f0, uint32_t f1, uint32_t f2)
{
  _last_f0 = f0;
  _last_f1 = f1;
  _last_f2 = f2;

  // Validate requested outputs (backend-specific, conservative)
  bool ok = true;
  if (f0 != 0) ok = ok && freq_ok_for_backend(f0);
  if (f1 != 0) ok = ok && freq_ok_for_backend(f1);
  if (f2 != 0) ok = ok && freq_ok_for_backend(f2);

  uint8_t mask = 0;
  if (f0 != 0) mask |= 0x01;
  if (f1 != 0) mask |= 0x02;
  if (f2 != 0) mask |= 0x04;

  // Program hardware even if "ok" is false (optional behavior).
  // If you'd rather "refuse to program", wrap this in: if (ok) { ... }
  si5351_set_freqs(f0, f1, f2, mask);

  return ok;
}

bool Si5351Compat::freq_ok_for_backend(uint32_t freq_hz) const
{
  if (freq_hz == 0) return true; // 0 means "disabled" in Pico-SWR behavior

  // tiny5351 model (as shipped): PLLA fixed at 30 * XTAL
  const uint32_t pll_hz = _xtal_hz * 30UL;

  // Avoid divide-by-zero and nonsense
  if (pll_hz == 0) return false;

  // tiny5351 uses integer quotient as ms_div (uint16_t).
  // Datasheet/general practice: multisynth divider must be >=4.
  // Upper bounds vary; conservative <= 900 keeps ms_div in a safe 16-bit range
  // and avoids extremely low output frequencies.
  //
  // If you want closer alignment with your old library, change these bounds.
  uint32_t div = pll_hz / freq_hz;
  if (div < 4)   return false;
  if (div > 900) return false;

  // Also ensure freq_hz isn't above pll_hz/4 in practice (already covered by div>=4).
  return true;
}

bool Si5351Compat::is_freq_ok(uint8_t clk_num) const
{
  uint32_t f = 0;
  if (clk_num == 0) f = _last_f0;
  else if (clk_num == 1) f = _last_f1;
  else if (clk_num == 2) f = _last_f2;
  else return false;

  return freq_ok_for_backend(f);
}

#define XTAL_CALIBRATE_FREQ 10000000

void Si5351Compat::out_calibrate_freq()
{
  // Compatibility meaning:
  // Output a stable known reference so user can measure it and calibrate XTAL.
  // Here: drive CLK0 = XTAL, disable CLK1/2.
  //
  // If you prefer 10 MHz or 5 MHz, change this to that frequency instead.
//  (void)set_freq(_xtal_hz, 0, 0);
  (void)set_freq(XTAL_CALIBRATE_FREQ, 0, 0);
}


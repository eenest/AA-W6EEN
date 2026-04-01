// Full-featured library for Si5351
// version 1.0
// (c) Andrew Bilokon, UR5FFR
// mailto:ban.relayer@gmail.com
// http://dspview.com
// https://github.com/andrey-belokon
// Space savings mods by W6EEN

#include <inttypes.h>
#include "si5351a.h"
#include "i2c.h"

#define SI_CLK0_CONTROL 16      // Register definitions
#define SI_CLK1_CONTROL 17
#define SI_CLK2_CONTROL 18
#define SI_SYNTH_PLL_A  26
#define SI_SYNTH_PLL_B  34
#define SI_SYNTH_MS_0   42
#define SI_SYNTH_MS_1   50
#define SI_SYNTH_MS_2   58
#define SI_PLL_RESET    177

#define SI_PLL_RESET_A   0x20
#define SI_PLL_RESET_B   0x80

#define SI_CLK0_PHASE  165
#define SI_CLK1_PHASE  166
#define SI_CLK2_PHASE  167

#define R_DIV(x) ((x) << 4)

#define SI_CLK_SRC_PLL_A  0b00000000
#define SI_CLK_SRC_PLL_B  0b00100000

#define SI5351_I2C_ADDR 0x60

// 1048575
#define FRAC_DENOM 0xFFFFF

uint32_t Si5351::VCOFreq_Max = 900000000;
uint32_t Si5351::VCOFreq_Min = 600000000;
uint32_t Si5351::VCOFreq_Mid = 750000000;

static void si5351_write_reg(uint8_t reg, uint8_t data)
{
  i2c_begin_write(SI5351_I2C_ADDR);
  i2c_write(reg);
  i2c_write(data);
  i2c_end();
}

static void si5351_write_regs(uint8_t synth, uint32_t P1, uint32_t P2, uint32_t P3, uint8_t rDiv, bool divby4)
{
  i2c_begin_write(SI5351_I2C_ADDR);
  i2c_write(synth);
  i2c_write(((uint8_t*)&P3)[1]);
  i2c_write((uint8_t)P3);
  i2c_write((((uint8_t*)&P1)[2] & 0x3) | rDiv | (divby4 ? 0x0C : 0x00));
  i2c_write(((uint8_t*)&P1)[1]);
  i2c_write((uint8_t)P1);
  i2c_write(((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
  i2c_write(((uint8_t*)&P2)[1]);
  i2c_write((uint8_t)P2);
  i2c_end();
}

// Set up MultiSynth with mult, num and denom
// mult is 15..90
// num is 0..1,048,575 (0xFFFFF)
// denom is 0..1,048,575 (0xFFFFF)
static void si5351_setup_msynth(uint8_t synth, uint8_t a, uint32_t b, uint32_t c, uint8_t rDiv)
{
  uint32_t t = 128 * b / c;
  si5351_write_regs(
    synth,
    (uint32_t)(128UL * (uint32_t)(a) + t - 512UL),
    (uint32_t)(128UL * b - c * t),
    c,
    rDiv,
    false
  );
}

// Set up MultiSynth with integer divider and R divider
static void si5351_setup_msynth_int(uint8_t synth, uint32_t divider, uint8_t rDiv)
{
  // P2 = 0, P3 = 1 forces an integer value for the divider
  si5351_write_regs(
    synth,
    128UL * divider - 512UL,
    0,
    1,
    rDiv,
    divider == 4
  );
}

void Si5351::setup(uint8_t _power0, uint8_t _power1, uint8_t _power2)
{
  power0 = _power0 & 0x3;
  power1 = _power1 & 0x3;
  power2 = _power2 & 0x3;
  si5351_write_reg(SI_CLK0_CONTROL, 0x80);
  si5351_write_reg(SI_CLK1_CONTROL, 0x80);
  si5351_write_reg(SI_CLK2_CONTROL, 0x80);
  VCOFreq_Mid = (VCOFreq_Min + VCOFreq_Max) >> 1;
}

void Si5351::set_power(uint8_t _power0, uint8_t _power1, uint8_t _power2)
{
  uint32_t f0 = freq0, f1 = freq1, f2 = freq2;
  power0 = _power0 & 0x3;
  power1 = _power1 & 0x3;
  power2 = _power2 & 0x3;
  freq0 = freq1 = freq2 = 0;

#if SI5351_CLK2_DISABLE
  // Force CLK2 disabled in W6EEN build.
  f2 = 0;
#endif

  set_freq(f0, f1, f2);
}

void Si5351::set_xtal_freq(uint32_t freq, uint8_t reset_pll)
{
  uint8_t need_reset_pll;
  xtal_freq = freq * 10UL;
  update_freq0(&need_reset_pll);
  update_freq12(1, &need_reset_pll);
  if (reset_pll) si5351_write_reg(SI_PLL_RESET, 0xA0);
}

uint8_t Si5351::set_freq(uint32_t f0, uint32_t f1, uint32_t f2)
{
  uint8_t need_reset_pll = 0;
  uint8_t freq1_changed = (f1 != freq1);

  if (f0 != freq0) {
    freq0 = f0;
    update_freq0(&need_reset_pll);
  }

#if SI5351_CLK2_DISABLE
  // CLK2 compiled out: ignore f2 and keep CLK2 disabled.
  (void)f2;

  if (freq1_changed) {
    freq1 = f1;
    freq2 = 0;
    update_freq12(1, &need_reset_pll);
  }
#else
  if (freq1_changed || f2 != freq2) {
    freq1 = f1;
    freq2 = f2;
    update_freq12(freq1_changed, &need_reset_pll);
  }
#endif

  if (need_reset_pll)
    si5351_write_reg(SI_PLL_RESET, need_reset_pll);

  return need_reset_pll;
}

void Si5351::disable_out(uint8_t clk_num)
{
  switch (clk_num) {
    case 0:
      si5351_write_reg(SI_CLK0_CONTROL, 0x80);
      freq0_div = 0;
      break;
    case 1:
      si5351_write_reg(SI_CLK1_CONTROL, 0x80);
      freq1_div = 0;
      break;
    case 2:
      si5351_write_reg(SI_CLK2_CONTROL, 0x80);
      freq2_div = 0;
      break;
  }
}

uint8_t Si5351::is_freq_ok(uint8_t clk_num)
{
  switch (clk_num) {
    case 0: return freq0_div != 0;
    case 1: return freq1_div != 0;
    case 2: return freq2_div != 0;
  }
  return false;
}

void Si5351::out_calibrate_freq()
{
  si5351_write_reg(SI_CLK0_CONTROL, power0);
  si5351_write_reg(SI_CLK1_CONTROL, power1);
  si5351_write_reg(SI_CLK2_CONTROL, power2);
  si5351_write_reg(SI_SYNTH_MS_0 + 2, 0);
  si5351_write_reg(SI_SYNTH_MS_1 + 2, 0);
  si5351_write_reg(SI_SYNTH_MS_2 + 2, 0);
  si5351_write_reg(187, 0xD0);
  freq0 = freq1 = freq2 = xtal_freq;
}

void Si5351::update_freq0(uint8_t* need_reset_pll)
{
  uint64_t pll_freq;
  uint8_t  mult;
  uint32_t num;
  uint32_t divider;
  uint8_t  rdiv = 0;

  if (freq0 == 0) {
    disable_out(0);
    return;
  }

  // try to use last divider
  divider = freq0_div;
  rdiv = freq0_rdiv;
  pll_freq = (uint64_t)divider * (uint64_t)freq0 * (uint64_t)(1UL << rdiv);

  if (pll_freq < VCOFreq_Min || pll_freq > VCOFreq_Max) {
    divider = VCOFreq_Mid / freq0;
    if (divider < 4) {
      disable_out(0);
      return;
    }

    if (divider < 6)
      divider = 4;

    rdiv = 0;
    while (divider > 300) {
      rdiv++;
      divider >>= 1;
    }
    if (rdiv == 0) divider &= 0xFFFFFFFEUL;

    pll_freq = (uint64_t)divider * (uint64_t)freq0 * (uint64_t)(1UL << rdiv);
  }

  mult = (uint64_t)pll_freq * 10ULL / xtal_freq;
  num  = (uint64_t)(pll_freq - (uint64_t)mult * (uint64_t)xtal_freq / 10ULL) * (uint64_t)FRAC_DENOM * 10ULL / xtal_freq;

  si5351_setup_msynth(SI_SYNTH_PLL_A, mult, num, FRAC_DENOM, 0);

  if (divider != freq0_div || rdiv != freq0_rdiv) {
    si5351_setup_msynth_int(SI_SYNTH_MS_0, divider, R_DIV(rdiv));
    si5351_write_reg(SI_CLK0_CONTROL, 0x4C | power0 | SI_CLK_SRC_PLL_A);
    freq0_div = divider;
    freq0_rdiv = rdiv;
    *need_reset_pll |= SI_PLL_RESET_A;
  }
}

void Si5351::update_freq12(uint8_t freq1_changed, uint8_t* need_reset_pll)
{
  uint64_t pll_freq;
  uint8_t  mult;
  uint32_t num;
  uint32_t divider;
  uint8_t  rdiv = 0;

  if (freq1 == 0) {
    disable_out(1);
  }

#if SI5351_CLK2_DISABLE
  // W6EEN build: CLK2 not used; always keep it disabled and compile out CLK2 logic paths.
  disable_out(2);
#else
  if (freq2 == 0) {
    disable_out(2);
  }
#endif

  if (freq1) {
    if (freq1_changed) {
      // try to use last divider
      divider = freq1_div;
      rdiv = freq1_rdiv;
      pll_freq = (uint64_t)divider * (uint64_t)freq1 * (uint64_t)(1UL << rdiv);

      if (pll_freq < VCOFreq_Min || pll_freq > VCOFreq_Max) {
        divider = VCOFreq_Mid / freq1;
        if (divider < 4) {
          disable_out(1);
          return;
        }
        if (divider < 6)
          divider = 4;
        rdiv = 0;
        while (divider > 300) {
          rdiv++;
          divider >>= 1;
        }
        if (rdiv == 0) divider &= 0xFFFFFFFEUL;

        pll_freq = (uint64_t)divider * (uint64_t)freq1 * (uint64_t)(1UL << rdiv);
      }

      mult = (uint64_t)pll_freq * 10ULL / xtal_freq;
      num  = (uint64_t)(pll_freq - (uint64_t)mult * (uint64_t)xtal_freq / 10ULL) * (uint64_t)FRAC_DENOM * 10ULL / xtal_freq;

      si5351_setup_msynth(SI_SYNTH_PLL_B, mult, num, FRAC_DENOM, 0);
      if (divider != freq1_div || rdiv != freq1_rdiv) {
        si5351_setup_msynth_int(SI_SYNTH_MS_1, divider, R_DIV(rdiv));
        si5351_write_reg(SI_CLK1_CONTROL, 0x4C | power1 | SI_CLK_SRC_PLL_B);
        freq1_div = divider;
        freq1_rdiv = rdiv;
        *need_reset_pll |= SI_PLL_RESET_B;
      }
      freq_pll_b = (uint32_t)pll_freq;
    }

#if !SI5351_CLK2_DISABLE
    if (freq2) {
      // CLK2 --> PLL_B with fractional or integer multisynth
      divider = freq_pll_b / freq2;
      if (divider < 8) {
        disable_out(2);
        return;
      }
      rdiv = 0;
      uint32_t ff = freq2;
      while (divider > 64) {
        rdiv++;
        ff <<= 1;
        divider >>= 1;
      }
      divider = freq_pll_b / ff;
      num = (uint64_t)(freq_pll_b % ff) * FRAC_DENOM / ff;

      si5351_setup_msynth(SI_SYNTH_MS_2, divider, num, (num ? FRAC_DENOM : 1), R_DIV(rdiv));
      si5351_write_reg(SI_CLK2_CONTROL, (num ? 0x0C : 0x4C) | power2 | SI_CLK_SRC_PLL_B);
      freq2_div = 1; // non zero for correct enable/disable CLK2
    }
#endif

#if !SI5351_CLK2_DISABLE
  } else if (freq2) {
    // PLL_B --> CLK2, multisynth integer
    // try to use last divider
    divider = freq2_div;
    rdiv = freq2_rdiv;
    pll_freq = (uint64_t)divider * (uint64_t)freq2 * (uint64_t)(1UL << rdiv);

    if (pll_freq < VCOFreq_Min || pll_freq > VCOFreq_Max) {
      divider = VCOFreq_Mid / freq2;
      if (divider < 4) {
        disable_out(2);
        return;
      }
      if (divider < 6)
        divider = 4;
      rdiv = 0;
      while (divider > 300) {
        rdiv++;
        divider >>= 1;
      }
      if (rdiv == 0) divider &= 0xFFFFFFFEUL;

      pll_freq = (uint64_t)divider * (uint64_t)freq2 * (uint64_t)(1UL << rdiv);
    }

    mult = (uint64_t)pll_freq * 10ULL / xtal_freq;
    num  = (uint64_t)(pll_freq - (uint64_t)mult * (uint64_t)xtal_freq / 10ULL) * (uint64_t)FRAC_DENOM * 10ULL / xtal_freq;

    si5351_setup_msynth(SI_SYNTH_PLL_B, mult, num, FRAC_DENOM, 0);

    if (divider != freq2_div || rdiv != freq2_rdiv) {
      si5351_setup_msynth_int(SI_SYNTH_MS_2, divider, R_DIV(rdiv));
      si5351_write_reg(SI_CLK2_CONTROL, 0x4C | power2 | SI_CLK_SRC_PLL_B);
      freq2_div = divider;
      freq2_rdiv = rdiv;
      *need_reset_pll |= SI_PLL_RESET_B;
    }
#endif
  }
}

#if !SI5351_QUADRATURE_DISABLE
void Si5351::update_freq01_quad(uint8_t* need_reset_pll)
{
  uint64_t pll_freq;
  uint8_t  mult;
  uint32_t num;
  uint32_t divider;

  if (freq0 == 0) {
    disable_out(0);
    disable_out(1);
    return;
  }

  if (freq0 >= 7000000UL) {
    divider = (VCOFreq_Max / freq0);
  } else if (freq0 >= 4000000UL) {
    divider = (VCOFreq_Min / freq0);
  } else if (freq0 >= 2000000UL) {
    // VCO run on freq less than 600MHz. possible unstable
    // comment this for disable operation below 600MHz VCO (4MHz on out)
    divider = 0x7F;
  } else {
    divider = 0; // disable out on invalid freq
  }

  if (divider < 4 || divider > 0x7F) {
    disable_out(0);
    disable_out(1);
    return;
  }

  if (divider < 6)
    divider = 4;

  pll_freq = (uint64_t)divider * (uint64_t)freq0;

  mult = (uint64_t)pll_freq * 10ULL / xtal_freq;
  num  = (uint64_t)(pll_freq - (uint64_t)mult * (uint64_t)xtal_freq / 10ULL) * (uint64_t)FRAC_DENOM * 10ULL / xtal_freq;

  si5351_setup_msynth(SI_SYNTH_PLL_A, mult, num, FRAC_DENOM, 0);

  if (divider != freq0_div) {
    si5351_setup_msynth_int(SI_SYNTH_MS_0, divider, 0);
    si5351_write_reg(SI_CLK0_CONTROL, 0x4C | power0 | SI_CLK_SRC_PLL_A);
    si5351_write_reg(SI_CLK0_PHASE, 0);

    si5351_setup_msynth_int(SI_SYNTH_MS_1, divider, 0);
    si5351_write_reg(SI_CLK1_CONTROL, 0x4C | power0 | SI_CLK_SRC_PLL_A);
    si5351_write_reg(SI_CLK1_PHASE, divider & 0x7F);

    freq0_div = freq1_div = divider;
    *need_reset_pll |= SI_PLL_RESET_A;
  }
}
#endif

#if !SI5351_CLK2_DISABLE
void Si5351::update_freq2(uint8_t* need_reset_pll)
{
  uint64_t pll_freq;
  uint8_t  mult;
  uint32_t num;
  uint32_t divider;
  uint8_t  rdiv = 0;

  if (freq2 == 0) {
    disable_out(2);
    return;
  }

  // PLL_B --> CLK2, multisynth integer
  // try to use last divider
  divider = freq2_div;
  rdiv = freq2_rdiv;
  pll_freq = (uint64_t)divider * (uint64_t)freq2 * (uint64_t)(1UL << rdiv);

  if (pll_freq < VCOFreq_Min || pll_freq > VCOFreq_Max) {
    divider = VCOFreq_Mid / freq2;
    if (divider < 4) {
      disable_out(2);
      return;
    }

    if (divider < 6)
      divider = 4;

    rdiv = 0;
    while (divider > 300) {
      rdiv++;
      divider >>= 1;
    }
    if (rdiv == 0) divider &= 0xFFFFFFFEUL;

    pll_freq = (uint64_t)divider * (uint64_t)freq2 * (uint64_t)(1UL << rdiv);
  }

  mult = (uint64_t)pll_freq * 10ULL / xtal_freq;
  num  = (uint64_t)(pll_freq - (uint64_t)mult * (uint64_t)xtal_freq / 10ULL) * (uint64_t)FRAC_DENOM * 10ULL / xtal_freq;

  si5351_setup_msynth(SI_SYNTH_PLL_B, mult, num, FRAC_DENOM, 0);

  if (divider != freq2_div || rdiv != freq2_rdiv) {
    si5351_setup_msynth_int(SI_SYNTH_MS_2, divider, R_DIV(rdiv));
    si5351_write_reg(SI_CLK2_CONTROL, 0x4C | power2 | SI_CLK_SRC_PLL_B);
    freq2_div = divider;
    freq2_rdiv = rdiv;
    *need_reset_pll |= SI_PLL_RESET_B;
  }
}
#endif

#if !SI5351_QUADRATURE_DISABLE
uint8_t Si5351::set_freq_quadrature(uint32_t f01, uint32_t f2)
{
  uint8_t need_reset_pll = 0;

  if (f01 != freq0) {
    freq0 = f01;
    update_freq01_quad(&need_reset_pll);
  }

#if !SI5351_CLK2_DISABLE
  if (f2 != freq2) {
    freq2 = f2;
    update_freq2(&need_reset_pll);
  }
#else
  (void)f2;
#endif

  if (need_reset_pll)
    si5351_write_reg(SI_PLL_RESET, need_reset_pll);

  return need_reset_pll;
}
#endif

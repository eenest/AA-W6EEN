#ifndef TINY5351_H
#define TINY5351_H

#include <inttypes.h>

/*
 * Derived from the tiny5351 library:
 * https://github.com/riyas-org/tiny5351
 *
 * Upstream note:
 * Borrowed from https://github.com/threeme3/QCX-SSB
 *
 * Modified in 2025 by Eugene Nesterenko, W6EEN.
 */

/* Borrowed from https://github.com/threeme3/QCX-SSB */

#define SI_CLK_OE 3     // Register definitions
#define SI_CLK0_PHOFF 165
#define SI_CLK1_PHOFF 166
#define SI_CLK2_PHOFF 167
#define SI_XTAL_FREQ 25000000 // Measured crystal frequency of XTAL2 for CL = 10pF


#define SI_CLK0_CONTROL	16			// Register definitions
#define SI_CLK1_CONTROL	17
#define SI_CLK2_CONTROL	18
#define SI_SYNTH_PLL_A	26
#define SI_SYNTH_PLL_B	34
#define SI_SYNTH_MS_0		42
#define SI_SYNTH_MS_1		50
#define SI_SYNTH_MS_2		58
#define SI_PLL_RESET		177

#define SI_R_DIV_1		0b00000000			// R-division ratio definitions
#define SI_R_DIV_2		0b00010000
#define SI_R_DIV_4		0b00100000
#define SI_R_DIV_8		0b00110000
#define SI_R_DIV_16		0b01000000
#define SI_R_DIV_32		0b01010000
#define SI_R_DIV_64		0b01100000
#define SI_R_DIV_128	0b01110000
#define R_DIV_NA		0b11111111

#define SI_CLK_SRC_PLL_A	0b00000000
#define SI_CLK_SRC_PLL_B	0b00100000

#define XTAL_FREQ	25000000
#ifdef __cplusplus
extern "C"{
#endif			// Crystal frequency
void si5351_freq(uint32_t freq,uint8_t clk);
// new functions
void si5351_set_xtal_freq(uint32_t xtal_hz);

/* Set multiple outputs in one transaction.
 * enable_mask: bit0=CLK0, bit1=CLK1, bit2=CLK2 (1 = enable)
 * f0/f1/f2 are in Hz; a freq of 0 disables that clock (even if mask bit is set).
 */
void si5351_set_freqs(uint32_t f0, uint32_t f1, uint32_t f2, uint8_t enable_mask);

void si5351_set_drives(uint8_t d0, uint8_t d1, uint8_t d2);

#ifdef __cplusplus
	}
#endif
#endif //TINY5351_H

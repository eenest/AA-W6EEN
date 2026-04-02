#include <inttypes.h>
#include <stdlib.h>
#include "i2cmaster.h"
#include "tiny5351.h"
#include "slimmath.h"

/*
 * Derived from the tiny5351 library:
 * https://github.com/riyas-org/tiny5351
 *
 * Modified in 2025 by Eugene Nesterenko, W6EEN.
 */


static uint32_t g_si_xtal_freq = SI_XTAL_FREQ;

void si5351_set_xtal_freq(uint32_t xtal_hz)
{
    if (xtal_hz != 0) g_si_xtal_freq = xtal_hz;
}

static uint8_t g_drive[3] = {0, 0, 0}; // default 2mA like Pico-SWR uses for CLK0

void si5351_set_drives(uint8_t d0, uint8_t d1, uint8_t d2)
{
    g_drive[0] = (uint8_t)(d0 & 0x03);
    g_drive[1] = (uint8_t)(d1 & 0x03);
    g_drive[2] = (uint8_t)(d2 & 0x03);
}

#define I2C_WRITE 0b11000000		
#define I2C_READ  0b11000001		


uint8_t i2cSendRegister(uint8_t reg, uint8_t data)
{
	i2c_start_wait(I2C_WRITE);
	i2c_write(reg);
	i2c_write(data);
	i2c_stop();	
	return 0;
}

static void setupPLL(uint8_t pll, uint16_t mult, uint32_t num, uint32_t denom, uint8_t rDiv)
{
	uint32_t P1;					
	uint32_t P2;					
	uint32_t P3;					

	uint32_t mulresult = num<<7;	
	div_result output=tdivide(mulresult,denom);
	uint32_t term =  output.quot;
	uint32_t mulresultmix= tmultiply(denom, term);
	P2 = mulresult - mulresultmix; 
	mulresult = ((uint32_t)mult) << 7;
	P1 = mulresult + term - 512;
	P3 = denom;
	i2cSendRegister(pll + 0, (P3 & 0x0000FF00) >> 8);
	i2cSendRegister(pll + 1, (P3 & 0x000000FF));
	if(rDiv!=R_DIV_NA)
		{
		i2cSendRegister(pll + 2,   ((P1 & 0x00030000) >> 16) | rDiv);
		}
	else
		{
		i2cSendRegister(pll + 2,   ((P1 & 0x00030000) >> 16));
		}
	i2cSendRegister(pll + 3, (P1 & 0x0000FF00) >> 8);
	i2cSendRegister(pll + 4, (P1 & 0x000000FF));
	i2cSendRegister(pll + 5, ((P3 & 0x000F0000) >> 12) | ((P2 & 0x000F0000) >> 16));
	i2cSendRegister(pll + 6, (P2 & 0x0000FF00) >> 8);
	i2cSendRegister(pll + 7, (P2 & 0x000000FF));	
}

static void program_clk_plla(uint32_t pll_freq, uint32_t freq, uint8_t clk)
{
    if (freq == 0) return;

    div_result output = tdivide(pll_freq, freq);
    uint16_t ms_div = (uint16_t)output.quot;

    // Guard against invalid divider ranges (prevents wrap/garbage output)
    if (ms_div < 4 || ms_div > 900) return;

    const uint32_t denom = 0xFFFFF;               // 1048575 (20-bit)
    uint32_t rem = (uint32_t)output.remainder;    // pll_freq - ms_div*freq

    // num = round(rem * denom / freq)
    uint32_t num = (uint32_t)((((uint64_t)rem * (uint64_t)denom) + (freq / 2)) / freq);

    // clamp: num must be < denom
    if (num >= denom) num = denom - 1;

    setupPLL((SI_SYNTH_MS_0 + (8 * clk)), ms_div, num, denom, SI_R_DIV_1);

    uint8_t base = (num ? 0x0C : 0x4C);              // fractional vs integer MS mode
    uint8_t drv  = (uint8_t)(g_drive[clk] & 0x03);   // drive strength 0..3
    i2cSendRegister((SI_CLK0_CONTROL + clk), (uint8_t)(base | drv | SI_CLK_SRC_PLL_A));
}

void si5351_set_freqs(uint32_t f0, uint32_t f1, uint32_t f2, uint8_t enable_mask)
{
    /* If freq is 0, force-disable the corresponding mask bit */
    if (f0 == 0) enable_mask &= (uint8_t)~0x01;
    if (f1 == 0) enable_mask &= (uint8_t)~0x02;
    if (f2 == 0) enable_mask &= (uint8_t)~0x04;

    i2c_init();

    /* Disable everything if nothing is enabled */
    if ((enable_mask & 0x07) == 0) {
        i2cSendRegister(SI_CLK_OE, 0xFF);
        i2c_exit();
        return;
    }

    /* PLLA fixed at 30 * XTAL (same concept as original tiny5351) */
    setupPLL(SI_SYNTH_PLL_A, 30, 0, 1, R_DIV_NA);

    uint32_t pll_freq = g_si_xtal_freq * 30UL;

    if (enable_mask & 0x01) program_clk_plla(pll_freq, f0, 0);
    if (enable_mask & 0x02) program_clk_plla(pll_freq, f1, 1);
    if (enable_mask & 0x04) program_clk_plla(pll_freq, f2, 2);

    /* Reset PLL once after programming */
    i2cSendRegister(SI_PLL_RESET, 0xA0);

    /* OE: bit=1 disables, bit=0 enables */
    uint8_t oe = 0xFF;
    oe &= (uint8_t)~(enable_mask & 0x07);
    i2cSendRegister(SI_CLK_OE, oe);

    i2c_exit();
}

void si5351_freq(uint32_t freq, uint8_t clk)
{
    uint32_t f0 = 0, f1 = 0, f2 = 0;
    uint8_t mask = 0;

    if (freq != 0) {
        mask = (uint8_t)(1U << clk);
        if (clk == 0) f0 = freq;
        else if (clk == 1) f1 = freq;
        else if (clk == 2) f2 = freq;
    }

    /* This wrapper keeps original behavior: one clock at a time */
    si5351_set_freqs(f0, f1, f2, mask);
}


//void si5351_freq(uint32_t freq,uint8_t clk)//, uint8_t i, uint8_t q
//{
//  uint8_t si5351_mult;
//  int32_t pll_freq; 
//  uint8_t r_div = 1;  
//  i2c_init();						
//  setupPLL(SI_SYNTH_PLL_A, 30, 0, 1,R_DIV_NA); 
//  //setupPLL(SI_SYNTH_PLL_B, 30, 0, 1,R_DIV_NA);
//  pll_freq=(SI_XTAL_FREQ*30);  
//  div_result output=tdivide(pll_freq,freq);   
//  si5351_mult = output.quot;
//  uint32_t l=  output.remainder<<7;
//  output=tdivide(l,freq); 
//  l=output.quot;
//  l= l << 13;
//  uint32_t num = l;     
//  const uint32_t denom = 0xFFFFF;  
//  setupPLL((SI_SYNTH_MS_0+(8*clk)), si5351_mult,num,denom, SI_R_DIV_1);
//  i2cSendRegister((SI_CLK0_CONTROL+clk), 0x4F | SI_CLK_SRC_PLL_A); //0x4F
//  i2cSendRegister(SI_PLL_RESET, 0xA0);   
//  i2cSendRegister(SI_CLK_OE,  ~(1 << clk)); // Enable
//  i2c_exit();						// Exit I2C  
//}
//

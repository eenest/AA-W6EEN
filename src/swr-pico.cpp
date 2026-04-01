// swr-pico.cpp
// Pico-SWR measurement core + Si5351 control + ZeroLevel EEPROM

#include "swr-pico.h"

#include <Arduino.h>

#include <math.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

#include "si5351_compat.h"
#include "i2cmaster.h"

// ============================================================
// Pico-SWR measurement parameters (from SWR_Pico.ino)
// ============================================================
#define AF_FREQ        1000
#define SAMPLING_K        16
#define SAMPLING_FREQ  (SAMPLING_K * AF_FREQ)

// Goertzel window length
#define N 256

// Precomputed Goertzel coefficient for SAMPLING_K=16, N=256, AF_FREQ=1000Hz, Fs=16000Hz
static float coeff = 1.847759065f;

// ============================================================
// Si5351 configuration
// ============================================================
// Output drive strengths: 0=2mA, 1=4mA, 2=6mA, 3=8mA
#define SI5351_CLK0_DRIVE       0
#define SI5351_CLK1_DRIVE       3
#define SI5351_CLK2_DRIVE       0

// ============================================================
// ADC input selection (AA_ADC_PIN)
// ============================================================
#ifndef AA_ADC_PIN
  #define AA_ADC_PIN A0
#endif

#if (AA_ADC_PIN < A0) || (AA_ADC_PIN > A3)
  #error "AA_ADC_PIN must be A0..A3 (A4/A5 are reserved for I2C)"
#endif

#define AA_ADC_CH ((uint8_t)(AA_ADC_PIN - A0))

// ============================================================
// ZeroLevel EEPROM table (SSOT rev22)
// ============================================================

#define CAL_MHZ_MIN  1u
#define CAL_MHZ_MAX  60u
#define CAL_STEP_HZ  500000UL

#define CAL_MIN_HZ   ((uint32_t)CAL_MHZ_MIN * 1000000UL)
#define CAL_MAX_HZ   ((uint32_t)CAL_MHZ_MAX * 1000000UL)
#define CAL_COUNT    (((CAL_MAX_HZ - CAL_MIN_HZ) / CAL_STEP_HZ) + 1u)

#define ZL_SIG_VALUE       0xA5u

#define ZL_HDR_BYTES       4u
#define ZL_ENTRY_BYTES     ((uint16_t)sizeof(float))
#define ZL_DATA_BYTES      ((uint16_t)CAL_COUNT * ZL_ENTRY_BYTES)
#define ZL_TOTAL_BYTES     (ZL_HDR_BYTES + ZL_DATA_BYTES)

#define ZL_EE_BASE         ((uint16_t)(E2END + 1u - ZL_TOTAL_BYTES))
#define ZL_EE_SIG_ADDR     ((uint16_t)(ZL_EE_BASE + 0u))
#define ZL_EE_COUNT_ADDR   ((uint16_t)(ZL_EE_BASE + 1u))
#define ZL_EE_CRC_ADDR     ((uint16_t)(ZL_EE_BASE + 2u))
#define ZL_EE_DATA_ADDR    ((uint16_t)(ZL_EE_BASE + 4u))

static bool    g_zl_inited = false;
static bool    g_zl_valid  = false;
static uint8_t g_zl_count  = 0;

static uint16_t fletcher16_eeprom(uint16_t eeAddr, uint16_t lenBytes)
{
  uint16_t sum1 = 0;
  uint16_t sum2 = 0;

  for (uint16_t i = 0; i < lenBytes; i++) {
    uint8_t b = eeprom_read_byte((const uint8_t*)(uintptr_t)(eeAddr + i));
    sum1 += b;
    sum1 %= 255;
    sum2 += sum1;
    sum2 %= 255;
  }

  return (uint16_t)((sum2 << 8) | sum1);
}

static uint16_t zl_entry_addr(uint16_t idx)
{
  return (uint16_t)(ZL_EE_DATA_ADDR + (uint16_t)idx * (uint16_t)sizeof(float));
}

static int16_t zl_index_from_freq(uint32_t frequencyHz)
{
  if (frequencyHz < CAL_MIN_HZ || frequencyHz > CAL_MAX_HZ) return -1;

  const uint32_t off = frequencyHz - CAL_MIN_HZ;

  // Nearest bucket: +step/2 before divide
  uint32_t idx = (off + (CAL_STEP_HZ / 2u)) / CAL_STEP_HZ;

  if (idx >= (uint32_t)CAL_COUNT) return -1;
  return (int16_t)idx;
}

bool verifyZeroLevelData(void)
{
  uint8_t sig   = eeprom_read_byte((const uint8_t*)(uintptr_t)ZL_EE_SIG_ADDR);
  uint8_t count = eeprom_read_byte((const uint8_t*)(uintptr_t)ZL_EE_COUNT_ADDR);

  if (sig != ZL_SIG_VALUE) return false;

  // Partial tables are NOT allowed.
  if (count != (uint8_t)CAL_COUNT) return false;

  uint16_t storedCrc = eeprom_read_word((const uint16_t*)(uintptr_t)ZL_EE_CRC_ADDR);
  uint16_t calcCrc   = fletcher16_eeprom(ZL_EE_DATA_ADDR, (uint16_t)CAL_COUNT * (uint16_t)sizeof(float));

  return (storedCrc == calcCrc);
}

static void zeroLevel_lazy_init(void)
{
  if (g_zl_inited) return;

  if (verifyZeroLevelData()) {
    g_zl_valid = true;
    g_zl_count = (uint8_t)CAL_COUNT;
  } else {
    g_zl_valid = false;
    g_zl_count = 0;
  }

  g_zl_inited = true;
}

void zeroLevel_factory_reset(void)
{
  // Clear header first (invalid)
  eeprom_update_byte((uint8_t*)(uintptr_t)ZL_EE_SIG_ADDR,   0);
  eeprom_update_byte((uint8_t*)(uintptr_t)ZL_EE_COUNT_ADDR, 0);
  eeprom_update_word((uint16_t*)(uintptr_t)ZL_EE_CRC_ADDR,  0);

  // Clear table entries to 0.0f
  float z = 0.0f;
  for (uint16_t i = 0; i < (uint16_t)CAL_COUNT; i++) {
    uint16_t a = zl_entry_addr(i);
    eeprom_update_block(&z, (void*)(uintptr_t)a, sizeof(float));
  }

  g_zl_inited = true;
  g_zl_valid  = false;
  g_zl_count  = 0;
}

bool saveZeroLevel(uint32_t frequencyHz, float value)
{
  zeroLevel_lazy_init();

  // write-protected once validated
  if (g_zl_valid) return false;

  int16_t idx = zl_index_from_freq(frequencyHz);
  if (idx < 0) return false;

  // Reserve 0.0f as invalid/unset
  if (!(value > 0.0f)) value = 0.0f;

  uint16_t a = zl_entry_addr((uint16_t)idx);
  eeprom_update_block(&value, (void*)(uintptr_t)a, sizeof(float));
  return true;
}

bool validateZeroLevelData(void)
{
  zeroLevel_lazy_init();

  // If already valid => do not allow overwriting; force factory reset first.
  if (g_zl_valid) return false;

  // Compute CRC over the full table
  uint16_t crc = fletcher16_eeprom(ZL_EE_DATA_ADDR, (uint16_t)CAL_COUNT * (uint16_t)sizeof(float));

  // Write header (signature last => atomic validity)
  eeprom_update_byte((uint8_t*)(uintptr_t)ZL_EE_COUNT_ADDR, (uint8_t)CAL_COUNT);
  eeprom_update_word((uint16_t*)(uintptr_t)ZL_EE_CRC_ADDR, crc);
  eeprom_update_byte((uint8_t*)(uintptr_t)ZL_EE_SIG_ADDR, ZL_SIG_VALUE);

  g_zl_valid = true;
  g_zl_count = (uint8_t)CAL_COUNT;
  return true;
}

float getZeroLevel(uint32_t frequencyHz)
{
  zeroLevel_lazy_init();

  if (!g_zl_valid) return 0.0f;

  int16_t idx = zl_index_from_freq(frequencyHz);
  if (idx < 0) return 0.0f;

  // Optional single-entry cache (minimizes EEPROM reads during sweep)
  static int16_t cache_idx = -1;
  static float   cache_val = 0.0f;

  if (idx != cache_idx) {
    uint16_t a = zl_entry_addr((uint16_t)idx);
    eeprom_read_block(&cache_val, (const void*)(uintptr_t)a, sizeof(float));
    cache_idx = idx;
  }

  return (cache_val > 0.0f) ? cache_val : 0.0f;
}

void zeroLevel_get_state(bool *valid, uint8_t *count)
{
  zeroLevel_lazy_init();

  if (valid) *valid = g_zl_valid;
  if (count) *count = g_zl_count;
}

// ============================================================
// Si5351 instance + cached output state
// ============================================================

static Si5351Compat vfo5351;
static uint32_t g_clk_freq_hz[2] = {0u, 0u};

// ============================================================
// Si5351 per-clock hard-disable (power down output driver)
// ============================================================
// tiny5351 already manages OE (output-enable) via SI_CLK_OE, but you requested
// an additional per-clock "Hi-Z/off" by powering down the output buffer.
// We do this by writing CLKx_PDN (bit7) in SI_CLKx_CONTROL.
//
// NOTE: tiny5351 uses fixed I2C address 0x60 (write byte 0b11000000).
#define AA_SI5351_I2C_WRITE 0b11000000

static void aa_si5351_write_reg(uint8_t reg, uint8_t data)
{
  i2c_start_wait(AA_SI5351_I2C_WRITE);
  i2c_write(reg);
  i2c_write(data);
  i2c_stop();
}

static void aa_si5351_force_clk_pdn(uint8_t clk)
{
  if (clk > 2) return;
  i2c_init();
  aa_si5351_write_reg((uint8_t)(SI_CLK0_CONTROL + clk), 0x80); // PDN=1
  i2c_exit();
}

// ============================================================
// Pico-SWR sampling ISR state
// ============================================================

// 0 - find min
// 1 - find max
// 2 - measure (Goertzel)
// 3 - stop
static volatile uint8_t measure_stage = 0;
static volatile int16_t last_val;
static volatile uint16_t nstep = 0;

static volatile float Q0, Q1, Q2;

EMPTY_INTERRUPT(TIMER1_COMPB_vect);

ISR(ADC_vect)
{
  int16_t val = (int16_t)ADC;

  switch (measure_stage) {
    case 0:
      if (val <= last_val) last_val = val;
      else measure_stage = 1;
      if (++nstep >= SAMPLING_K) {
        nstep = 0;
        measure_stage = 2;
      }
      break;

    case 1:
      if (val >= last_val) {
        last_val = val;
        if (++nstep < SAMPLING_K) break;
      }
      nstep = 0;
      measure_stage = 2;
      // fallthrough

    case 2:
      Q0 = coeff * Q1 - Q2 + (float)val;
      Q2 = Q1;
      Q1 = Q0;
      if (++nstep >= N) measure_stage = 3;
      break;

    case 3:
    default:
      break;
  }
}

// Internal: stop ADC trigger/interrupt + timer compare interrupt.
static void adc_timer_stop(void)
{
  // Stop timer & disable timer compare interrupt
  TCCR1B &= ~((1 << CS12) | (1 << CS11) | (1 << CS10));
  TIMSK1 &= ~(1 << OCIE1B);

  // Disable ADC auto-trigger + interrupt
  ADCSRA &= ~(bit(ADIE) | bit(ADATE));
  ADCSRA |= bit(ADIF); // clear any pending flag
}

// Internal: single measurement pass returning "Data" (energy at AF_FREQ).
static float readData_impl(void)
{
  nstep = 0;
  last_val = 10000;
  Q1 = 0;
  Q2 = 0;
  measure_stage = 0;

  // TIMER1 for interrupt frequency 16000 Hz:
  // 16MHz / (1 * 16000) - 1 = 999
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A  = 999;
  OCR1B  = 999;
  TCCR1B = bit(WGM12) | bit(CS10); // CTC, prescaler=1
  TIMSK1 = bit(OCIE1B);

  // ADC: prescaler=16, auto-trigger on Timer1 Compare Match B, 1.1V ref, AA_ADC_PIN
  ADCSRA = bit(ADEN) | bit(ADIE) | bit(ADIF) | bit(ADPS2);
  ADMUX  = (uint8_t)(0xC0 | (AA_ADC_CH & 0x07)); // REFS1:0=11 (1.1V), MUX=AA_ADC_CH
  ADCSRB = bit(ADTS0) | bit(ADTS2); // ADTS = 101 (Timer/Counter1 Compare Match B)
  ADCSRA |= bit(ADATE);

  while (measure_stage < 3) {
    // busy wait ~ (N / Fs) = 256/16000 = 16ms
  }

  adc_timer_stop();

  // Leave ADC enabled but with interrupts disabled and prescaler=16.
  ADCSRA = bit(ADEN) | bit(ADPS2);

  // Real part only:
  // Q1*Q1 + Q2*Q2 - Q1*Q2*coeff
  return (Q1 * Q1) + (Q2 * (Q2 - (Q1 * coeff)));
}

// ============================================================
// Public API
// ============================================================

void aaInit(uint32_t ref_xtal)
{
  // If caller doesn't provide reference XTAL, force a sane default.
  // (Matches the tiny5351 default used in Si5351Compat / tiny5351.)
  if (ref_xtal == 0u) {
#ifdef SI_XTAL_FREQ
    ref_xtal = (uint32_t)SI_XTAL_FREQ;
#else
    ref_xtal = 25000000UL;
#endif
  }

  vfo5351.VCOFreq_Max = 800000000UL; // allow unstable SI chips (original behavior)
  vfo5351.setup(
    SI5351_CLK0_DRIVE,
    SI5351_CLK1_DRIVE,
    SI5351_CLK2_DRIVE
  );
  vfo5351.set_xtal_freq(ref_xtal);

  pinMode(AA_ADC_PIN, INPUT);

  // Ensure measurement HW is not left running.
  aaStop();

  // Bring outputs to a known state.
  aaSetFreq(0, 0);

  // Prime ZeroLevel cached state (lazy init also works).
  (void)getZeroLevel(CAL_MIN_HZ);
}

void aaSetRefXtal(uint32_t ref_xtal)
{
  if (ref_xtal == 0u) return;

  vfo5351.set_xtal_freq(ref_xtal);

  // Re-apply last requested output frequencies so hardware updates immediately.
  uint32_t f0 = g_clk_freq_hz[0];
  uint32_t f1 = g_clk_freq_hz[1];

  (void)vfo5351.set_freq((long)f0, (long)f1, 0L);

}

void aaStop(void)
{
  adc_timer_stop();
}

bool aaSetFreq(int32_t frequency0Hz, int32_t frequency1Hz)
{
  uint32_t f0 = (frequency0Hz > 0) ? (uint32_t)frequency0Hz : 0u;
  uint32_t f1 = (frequency1Hz > 0) ? (uint32_t)frequency1Hz : 0u;

  g_clk_freq_hz[0] = f0;
  g_clk_freq_hz[1] = f1;

  bool ok = vfo5351.set_freq((long)f0, (long)f1, 0L);

  // Additional hard-disable (Hi-Z/off) for any disabled clock.
  if (f0 == 0u) aa_si5351_force_clk_pdn(0);
  if (f1 == 0u) aa_si5351_force_clk_pdn(1);
  // This module never uses CLK2; keep it powered down.
  aa_si5351_force_clk_pdn(2);

  return ok;
}

uint32_t aaGetFreq(int clk)
{
  if (clk == 1) return g_clk_freq_hz[1];
  return g_clk_freq_hz[0];
}

bool aaIsFreqOK(void)
{
  bool ok = true;
  if (g_clk_freq_hz[0] != 0u) ok = ok && vfo5351.is_freq_ok(0);
  if (g_clk_freq_hz[1] != 0u) ok = ok && vfo5351.is_freq_ok(1);
  return ok;
}

float aaReadData(void)
{
  return readData_impl();
}

float aaDataToZeroLevel(float data)
{
  if (!(data > 0.0f)) return 0.0f;
  return sqrtf(data) * 1000.0f;
}

bool aaComputeGammaAndSWR(float data, float zeroLevel, float *gamma, float *swr)
{
  if (!(data > 0.0f)) return false;
  if (!(zeroLevel > 0.0f)) return false;
  if (!gamma || !swr) return false;

  float g = (sqrtf(data) * 1000.0f) / zeroLevel;

  if (g < 0.0f) g = 0.0f;

  // SSOT: if Gamma >= 1, SWR is infinite/invalid (implementation-defined).
  // Here we clamp to avoid division-by-zero and return a very large SWR.
  if (g >= 0.9999f) g = 0.9999f;

  *gamma = g;
  *swr = (1.0f + g) / (1.0f - g);
  return true;
}

float aaReadSWR(uint32_t frequencyHz)
{
  // ZeroLevel must be valid and non-zero for this bucket.
  float zl = getZeroLevel(frequencyHz);
  if (!(zl > 0.0f)) return 0.0f;

  // Pico-SWR measurement needs two RF outputs separated by AF_FREQ.
  uint32_t f0 = frequencyHz;
  uint32_t f1 = (frequencyHz < 130000000UL) ? (frequencyHz + (uint32_t)AF_FREQ)
                                            : (frequencyHz - (uint32_t)AF_FREQ);

  if (!aaSetFreq((int32_t)f0, (int32_t)f1)) return 0.0f;
  if (!aaIsFreqOK()) return 0.0f;

  // Warm-up reads (matches original SWR_Pico.ino pattern: two reads discarded)
  (void)readData_impl();
  (void)readData_impl();

  float data = readData_impl();

  float gamma, swr;
  if (!aaComputeGammaAndSWR(data, zl, &gamma, &swr)) return 0.0f;

#if MEASUREMENT_DEBUG
  // CSV: MEAS,<MHz>,<ADC_data>,<ZeroLevel>,<Gamma>,<SWR>[,<FLAGS>]
  DBG_PRINT(F("MEAS,"));
  DBG_PRINTF((float)frequencyHz * 1e-6f, 4);

  DBG_PRINT(F(","));
  DBG_PRINTF((float)data, 2);

  DBG_PRINT(F(","));
  DBG_PRINTF((float)zl, 2);

  DBG_PRINT(F(","));
  DBG_PRINTF((float)gamma, 6);

  DBG_PRINT(F(","));
  DBG_PRINTLNF((float)swr, 2);
#endif

  return swr;
}



bool aaCalibrateZeroLevel(void)
{
  zeroLevel_factory_reset();

  // Set first frequency
  uint32_t cur_f0 = (uint32_t)CAL_MIN_HZ;
  uint32_t cur_f1 = cur_f0 + (uint32_t)AF_FREQ;

  if (!aaSetFreq((int32_t)cur_f0, (int32_t)cur_f1) || !aaIsFreqOK()) {
    aaSetFreq(0, 0);
    return false;
  }

  delay(50);
  (void)readData_impl();
  (void)readData_impl();

  // Calibrate from CAL_MIN_HZ..CAL_MAX_HZ with CAL_STEP_HZ steps (now 0.5 MHz)
  for (uint32_t f0 = (uint32_t)CAL_MIN_HZ; f0 <= (uint32_t)CAL_MAX_HZ; f0 += (uint32_t)CAL_STEP_HZ) {

    // At loop entry, Si5351 is already programmed to the current point (cur_f0/cur_f1).
    // The pre-set of the next point happens at the bottom of the loop.
    delay(50);

    float data = readData_impl();
    float zl   = aaDataToZeroLevel(data);

#if ZEROLEVEL_DEBUG
    // CSV: ZL,<MHz>,<ADC_data>,<ZeroLevel>
    DBG_PRINT(F("ZL,"));
    DBG_PRINTF((float)cur_f0 * 1e-6f, 2);   // prints 0.5 MHz steps as xx.xx
    DBG_PRINT(F(","));
    DBG_PRINTF((float)data, 2);
    DBG_PRINT(F(","));
    DBG_PRINTLNF((float)zl, 2);
#endif

    if (!(zl > 0.0f)) {
      aaSetFreq(0, 0);
      return false;
    }

    if (!saveZeroLevel(cur_f0, zl)) {
      aaSetFreq(0, 0);
      return false;
    }

    // You said you want this:
    delay(10);

    // Pre-set next freq and settle window
    uint32_t next_f0 = cur_f0 + (uint32_t)CAL_STEP_HZ;
    uint32_t next_f1 = next_f0 + (uint32_t)AF_FREQ;

    if (next_f0 <= (uint32_t)CAL_MAX_HZ) {

      if (!aaSetFreq((int32_t)next_f0, (int32_t)next_f1) || !aaIsFreqOK()) {
        aaSetFreq(0, 0);
        return false;
      }

      delay(50);
      (void)readData_impl();
      (void)readData_impl();
    }
    else {
      aaSetFreq(0, 0);
    }

    cur_f0 = next_f0;
    cur_f1 = next_f1;
  }

  const bool ok = validateZeroLevelData();

  aaSetFreq(0, 0);
  return ok;
}


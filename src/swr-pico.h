/*
 * (C) Eugene Nesterenko, W6EEN. 2025-2026
 * Based on code from Andrew Bilokon, UR5FFR.
 */

#pragma once
#include <Arduino.h>

#define DEBUG 0
#define ZEROLEVEL_DEBUG 0
#define MEASUREMENT_DEBUG 0
#define BATT_CAL_DEBUG 0
#define BATT_DEBUG 0

// ============================================================
// DEBUG (shared; define flags here)
// ============================================================
// Set to 1 to enable Serial debug output.
#ifndef DEBUG
#define DEBUG 0
#endif

// Set to 1 to print ZeroLevel calibration samples as they are collected.
#ifndef ZEROLEVEL_DEBUG
#define ZEROLEVEL_DEBUG 0
#endif

// Set to 1 to print measurement debug output (aaReadSWR).
#ifndef MEASUREMENT_DEBUG
#define MEASUREMENT_DEBUG 0
#endif

#if (DEBUG || ZEROLEVEL_DEBUG || MEASUREMENT_DEBUG)
  #define DBG_BEGIN(baud)        Serial.begin(baud)
  #define DBG_PRINT(x)           Serial.print(x)
  #define DBG_PRINTLN(x)         Serial.println(x)
  #define DBG_PRINTF(v,d)        Serial.print((v),(d))
  #define DBG_PRINTLNF(v,d)      Serial.println((v),(d))
#else
  #define DBG_BEGIN(baud)        do{}while(0)
  #define DBG_PRINT(x)           do{}while(0)
  #define DBG_PRINTLN(x)         do{}while(0)
  #define DBG_PRINTF(v,d)        do{}while(0)
  #define DBG_PRINTLNF(v,d)      do{}while(0)
#endif

#include <stdint.h>
#include <stdbool.h>

// -----------------------------
// Build-time configuration
// -----------------------------
// Define AA_ADC_PIN at compile time to choose which analog pin is sampled by the
// Pico-SWR ADC/Goertzel measurement code.
//
// Requirements (Arduino Nano/Uno / ATmega328P):
// - AA_ADC_PIN must be one of A0..A3
//   (A4/A5 are reserved for I2C to the Si5351)
//
// Example (platformio.ini):
//   build_flags = -DAA_ADC_PIN=A1

#ifdef __cplusplus
extern "C" {
#endif

// Initialize measurement + Si5351 subsystem.
// ref_xtal is the Si5351 reference crystal frequency in Hz.
// If ref_xtal == 0, a built-in default is used.
void aaInit(uint32_t ref_xtal);
void aaStop(void);

// Update Si5351 reference crystal frequency (Hz) without touching ADC/timers.
void aaSetRefXtal(uint32_t ref_xtal);

// Si5351 clocks:
//   CLK0 <- frequency0Hz
//   CLK1 <- frequency1Hz
// If either frequency is 0, that output is disabled.
bool aaSetFreq(int32_t frequency0Hz, int32_t frequency1Hz);

// Returns last requested frequency for CLK0 (clk=0) or CLK1 (clk=1), or 0 if disabled.
uint32_t aaGetFreq(int clk);

// Returns true if the Si5351 reports both currently-enabled outputs as OK.
bool aaIsFreqOK(void);

// Raw Pico-SWR measurement value ("Data" in SSOT). Useful for calibration sweeps.
float aaReadData(void);

// Helper: convert Data->ZeroLevel value (sqrt(Data)*1000), returns 0.0f on invalid input.
float aaDataToZeroLevel(float data);

// Helper: compute Gamma and SWR. Returns false if inputs invalid.
bool aaComputeGammaAndSWR(float data, float zeroLevel, float *gamma, float *swr);

// Returns SWR at the requested RF frequency (Hz).
// If the ZeroLevel table is not valid (not calibrated), returns 0.0f.
float aaReadSWR(uint32_t frequencyHz);

// -----------------------------
// ZeroLevel EEPROM table (SSOT rev22)
// -----------------------------
void  zeroLevel_factory_reset(void);
bool  verifyZeroLevelData(void);
bool  saveZeroLevel(uint32_t frequencyHz, float value);
bool  validateZeroLevelData(void);
float getZeroLevel(uint32_t frequencyHz);
void  zeroLevel_get_state(bool *valid, uint8_t *count);

bool aaCalibrateZeroLevel(void);

#ifdef __cplusplus
} // extern "C"
#endif

// minimal I2C library
// version 1.0
// based on code from QRP Labs
// (c) Andrew Bilokon, UR5FFR
// mailto:ban.relayer@gmail.com
// http://dspview.com
// https://github.com/andrey-belokon

#ifndef I2C_H
#define I2C_H

#include <inttypes.h>

// -----------------------------------------------------------------------------
// Build switches
//
// NOTE: Macros defined in the sketch (.ino) do NOT automatically apply to
// separately compiled .cpp files in Arduino builds. To ensure consistent
// compilation, we provide defaults here (can be overridden if defined earlier).
// -----------------------------------------------------------------------------

#ifndef I2C_CODE_READS
#define I2C_CODE_READS 0  // remove I2C reads to save space
#endif

// -----------------------------------------------------------------------------

void i2c_init(uint32_t i2c_freq = 100000);
bool i2c_begin_write(uint8_t addr);
bool i2c_write(uint8_t data);

#if I2C_CODE_READS
bool i2c_begin_read(uint8_t addr);
uint8_t i2c_read();
void i2c_read(uint8_t* data, uint8_t count);
void i2c_read_long(uint8_t* data, uint16_t count);
#endif

void i2c_end();
bool i2c_device_found(uint8_t addr);

#endif

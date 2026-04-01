#ifndef TGLIB_H
#define TGLIB_H

/*
 * Tiny Graphics Library (TGLIB) - minimal ST77xx driver
 *
 * Based on “Compact TFT Graphics Library v4”
 * David Johnson-Davies (technoblogy.com) - CC BY 4.0
 *
 * Sanitized/renamed with "tglib_" prefix.
 * Optimized for AVR (ATmega328P) streaming SPI.
 */

#include <stdint.h>
#include <avr/pgmspace.h>

/* ---- Compile-time pin config (enables fast direct-port writes on AVR) ----
 * Defaults match your original wiring:
 *   CS  = D10
 *   DC  = D8
 *   RST = D9
 *
 * You can override by defining these before including tglib.h:
 *   #define TGLIB_CS_PIN 7
 *   #define TGLIB_DC_PIN 6
 *   #define TGLIB_RST_PIN 5
 */
#ifndef TGLIB_CS_PIN
  #define TGLIB_CS_PIN 10
#endif
#ifndef TGLIB_DC_PIN
  #define TGLIB_DC_PIN 8
#endif
#ifndef TGLIB_RST_PIN
  #define TGLIB_RST_PIN 9
#endif

// ================== RGB565 16-color palette ==================
// Commonly used “basic 16” set (RGB565).
// Values are in 0xRRRR (RGB565): R[15:11], G[10:5], B[4:0]

static const uint16_t C_BLACK       = 0x0000;
static const uint16_t C_NAVY        = 0x000F;
static const uint16_t C_DARKGREEN   = 0x03E0;
static const uint16_t C_DARKCYAN    = 0x03EF;
static const uint16_t C_MAROON      = 0x7800;
static const uint16_t C_PURPLE      = 0x780F;
static const uint16_t C_OLIVE       = 0x7BE0;
static const uint16_t C_LIGHTGREY   = 0xC618;

static const uint16_t C_DARKGREY    = 0x7BEF;
static const uint16_t C_BLUE        = 0x001F;
static const uint16_t C_GREEN       = 0x07E0;
static const uint16_t C_CYAN        = 0x07FF;
static const uint16_t C_RED         = 0xF800;
static const uint16_t C_MAGENTA     = 0xF81F;
static const uint16_t C_YELLOW      = 0xFFE0;
static const uint16_t C_WHITE       = 0xFFFF;

/* ---- Display configuration constants (defined in tglib.cpp) ---- */
extern int const tglib_xsize;
extern int const tglib_ysize;
extern int const tglib_xoff;
extern int const tglib_yoff;
extern int const tglib_invert;
extern int const tglib_rotate;
extern int const tglib_bgr;

/* ---- Pin constants (defined in tglib.cpp) ---- */
extern int const tglib_cs;
extern int const tglib_dc;
extern int const tglib_rst;

/* ---- Drawing state ---- */
extern int tglib_xpos;
extern int tglib_ypos;
extern int tglib_fore;   // 16-bit RGB565
extern int tglib_back;   // 16-bit RGB565
extern int tglib_scale;  // text scale (1..n)

/* ---- Core API ---- */
void tglib_init(void);

void tglib_InitDisplay(void);
void tglib_DisplayOn(void);

void tglib_ClearDisplay(void);

void tglib_MoveTo(int x, int y);
void tglib_DrawTo(int x, int y);

void tglib_PlotPoint(int x, int y);
void tglib_FillRect(int w, int h);

void tglib_PlotChar(char c);
void tglib_PlotText(PGM_P s);
void tglib_PlotTextRam(const char *s);
void tglib_PlotInt(int n);

/* ---- Colour helper (RGB888 -> RGB565) ---- */
unsigned int tglib_Colour(int r, int g, int b);

#endif /* TGLIB_H */

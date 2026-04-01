/*
 * Tiny Graphics Library (TGLIB) - minimal ST77xx driver
 *
 * Based on “Compact TFT Graphics Library v4”
 * David Johnson-Davies (technoblogy.com) - CC BY 4.0
 *
 * Sanitized/renamed with "tglib_" prefix.
 * Optimized for AVR (ATmega328P) streaming SPI.
 */

#include <Arduino.h>
#include <SPI.h>
#include <avr/pgmspace.h>
#include <avr/io.h>

#include "tglib.h"

// ---------------------------------------------------------------------------
// Pin constants (wired defaults; can be overridden via TGLIB_*_PIN in header)
// ---------------------------------------------------------------------------
int const tglib_cs  = TGLIB_CS_PIN;
int const tglib_dc  = TGLIB_DC_PIN;
int const tglib_rst = TGLIB_RST_PIN;

// ---------------------------------------------------------------------------
// Display parameters
//
// Your current uploaded file’s active line was truncated, but it appears to be:
//   320x240, xoff=0, yoff=0, invert=0, rotate=0, bgr=1
//
// If your module needs different offsets/rotate/invert, edit the active line.
// ---------------------------------------------------------------------------

// Active config (edit if needed)
int const tglib_xsize  = 320;
int const tglib_ysize  = 240;
int const tglib_xoff   = 0;
int const tglib_yoff   = 0;
int const tglib_invert = 0;
int const tglib_rotate = 0;
int const tglib_bgr    = 0;

// ---------------------------------------------------------------------------
// ST77xx command constants
// ---------------------------------------------------------------------------
int const tglib_CASET = 0x2A; // Column address set
int const tglib_RASET = 0x2B; // Row address set
int const tglib_RAMWR = 0x2C; // Memory write

// ---------------------------------------------------------------------------
// Globals - current plot position and colours
// ---------------------------------------------------------------------------
int tglib_xpos = 0;
int tglib_ypos = 0;
int tglib_fore = 0xFFFF; // White
int tglib_back = 0x0000; // Black
int tglib_scale = 1;

// ---------------------------------------------------------------------------
// 5x7 font (6 columns including spacing), stored as columns, MSB at top.
// Index is ASCII-32.
// ---------------------------------------------------------------------------
static const uint8_t tglib_CharMap[96][6] PROGMEM = {
  { 0x00,0x00,0x00,0x00,0x00,0x00 }, // ' '
  { 0x00,0x00,0x5F,0x00,0x00,0x00 }, // '!'
  { 0x00,0x07,0x00,0x07,0x00,0x00 }, // '"'
  { 0x14,0x7F,0x14,0x7F,0x14,0x00 }, // '#'
  { 0x24,0x2A,0x7F,0x2A,0x12,0x00 }, // '$'
  { 0x23,0x13,0x08,0x64,0x62,0x00 }, // '%'
  { 0x36,0x49,0x55,0x22,0x50,0x00 }, // '&'
  { 0x00,0x05,0x03,0x00,0x00,0x00 }, // '''
  { 0x00,0x1C,0x22,0x41,0x00,0x00 }, // '('
  { 0x00,0x41,0x22,0x1C,0x00,0x00 }, // ')'
  { 0x14,0x08,0x3E,0x08,0x14,0x00 }, // '*'
  { 0x08,0x08,0x3E,0x08,0x08,0x00 }, // '+'
  { 0x00,0x50,0x30,0x00,0x00,0x00 }, // ','
  { 0x08,0x08,0x08,0x08,0x08,0x00 }, // '-'
  { 0x00,0x60,0x60,0x00,0x00,0x00 }, // '.'
  { 0x20,0x10,0x08,0x04,0x02,0x00 }, // '/'
  { 0x3E,0x51,0x49,0x45,0x3E,0x00 }, // '0'
  { 0x00,0x42,0x7F,0x40,0x00,0x00 }, // '1'
  { 0x72,0x49,0x49,0x49,0x46,0x00 }, // '2'
  { 0x21,0x41,0x49,0x4D,0x33,0x00 }, // '3'
  { 0x18,0x14,0x12,0x7F,0x10,0x00 }, // '4'
  { 0x27,0x45,0x45,0x45,0x39,0x00 }, // '5'
  { 0x3C,0x4A,0x49,0x49,0x31,0x00 }, // '6'
  { 0x41,0x21,0x11,0x09,0x07,0x00 }, // '7'
  { 0x36,0x49,0x49,0x49,0x36,0x00 }, // '8'
  { 0x46,0x49,0x49,0x29,0x1E,0x00 }, // '9'
  { 0x00,0x36,0x36,0x00,0x00,0x00 }, // ':'
  { 0x00,0x56,0x36,0x00,0x00,0x00 }, // ';'
  { 0x08,0x14,0x22,0x41,0x00,0x00 }, // '<'
  { 0x14,0x14,0x14,0x14,0x14,0x00 }, // '='
  { 0x00,0x41,0x22,0x14,0x08,0x00 }, // '>'
  { 0x02,0x01,0x59,0x09,0x06,0x00 }, // '?'
  { 0x3E,0x41,0x5D,0x59,0x4E,0x00 }, // '@'
  { 0x7C,0x12,0x11,0x12,0x7C,0x00 }, // 'A'
  { 0x7F,0x49,0x49,0x49,0x36,0x00 }, // 'B'
  { 0x3E,0x41,0x41,0x41,0x22,0x00 }, // 'C'
  { 0x7F,0x41,0x41,0x22,0x1C,0x00 }, // 'D'
  { 0x7F,0x49,0x49,0x49,0x41,0x00 }, // 'E'
  { 0x7F,0x09,0x09,0x09,0x01,0x00 }, // 'F'
  { 0x3E,0x41,0x41,0x51,0x32,0x00 }, // 'G'
  { 0x7F,0x08,0x08,0x08,0x7F,0x00 }, // 'H'
  { 0x00,0x41,0x7F,0x41,0x00,0x00 }, // 'I'
  { 0x20,0x40,0x41,0x3F,0x01,0x00 }, // 'J'
  { 0x7F,0x08,0x14,0x22,0x41,0x00 }, // 'K'
  { 0x7F,0x40,0x40,0x40,0x40,0x00 }, // 'L'
  { 0x7F,0x02,0x0C,0x02,0x7F,0x00 }, // 'M'
  { 0x7F,0x04,0x08,0x10,0x7F,0x00 }, // 'N'
  { 0x3E,0x41,0x41,0x41,0x3E,0x00 }, // 'O'
  { 0x7F,0x09,0x09,0x09,0x06,0x00 }, // 'P'
  { 0x3E,0x41,0x51,0x21,0x5E,0x00 }, // 'Q'
  { 0x7F,0x09,0x19,0x29,0x46,0x00 }, // 'R'
  { 0x46,0x49,0x49,0x49,0x31,0x00 }, // 'S'
  { 0x01,0x01,0x7F,0x01,0x01,0x00 }, // 'T'
  { 0x3F,0x40,0x40,0x40,0x3F,0x00 }, // 'U'
  { 0x1F,0x20,0x40,0x20,0x1F,0x00 }, // 'V'
  { 0x3F,0x40,0x38,0x40,0x3F,0x00 }, // 'W'
  { 0x63,0x14,0x08,0x14,0x63,0x00 }, // 'X'
  { 0x07,0x08,0x70,0x08,0x07,0x00 }, // 'Y'
  { 0x61,0x51,0x49,0x45,0x43,0x00 }, // 'Z'
  { 0x00,0x7F,0x41,0x41,0x00,0x00 }, // '['
  { 0x02,0x04,0x08,0x10,0x20,0x00 }, // '\'
  { 0x00,0x41,0x41,0x7F,0x00,0x00 }, // ']'
  { 0x04,0x02,0x01,0x02,0x04,0x00 }, // '^'
  { 0x40,0x40,0x40,0x40,0x40,0x00 }, // '_'
  { 0x00,0x03,0x05,0x00,0x00,0x00 }, // '`'
  { 0x20,0x54,0x54,0x54,0x78,0x00 }, // 'a'
  { 0x7F,0x48,0x44,0x44,0x38,0x00 }, // 'b'
  { 0x38,0x44,0x44,0x44,0x20,0x00 }, // 'c'
  { 0x38,0x44,0x44,0x48,0x7F,0x00 }, // 'd'
  { 0x38,0x54,0x54,0x54,0x18,0x00 }, // 'e'
  { 0x08,0x7E,0x09,0x01,0x02,0x00 }, // 'f'
  { 0x0C,0x52,0x52,0x52,0x3E,0x00 }, // 'g'
  { 0x7F,0x08,0x04,0x04,0x78,0x00 }, // 'h'
  { 0x00,0x44,0x7D,0x40,0x00,0x00 }, // 'i'
  { 0x20,0x40,0x44,0x3D,0x00,0x00 }, // 'j'
  { 0x7F,0x10,0x28,0x44,0x00,0x00 }, // 'k'
  { 0x00,0x41,0x7F,0x40,0x00,0x00 }, // 'l'
  { 0x7C,0x04,0x18,0x04,0x78,0x00 }, // 'm'
  { 0x7C,0x08,0x04,0x04,0x78,0x00 }, // 'n'
  { 0x38,0x44,0x44,0x44,0x38,0x00 }, // 'o'
  { 0x7C,0x14,0x14,0x14,0x08,0x00 }, // 'p'
  { 0x08,0x14,0x14,0x18,0x7C,0x00 }, // 'q'
  { 0x7C,0x08,0x04,0x04,0x08,0x00 }, // 'r'
  { 0x48,0x54,0x54,0x54,0x20,0x00 }, // 's'
  { 0x04,0x3F,0x44,0x40,0x20,0x00 }, // 't'
  { 0x3C,0x40,0x40,0x20,0x7C,0x00 }, // 'u'
  { 0x1C,0x20,0x40,0x20,0x1C,0x00 }, // 'v'
  { 0x3C,0x40,0x30,0x40,0x3C,0x00 }, // 'w'
  { 0x44,0x28,0x10,0x28,0x44,0x00 }, // 'x'
  { 0x0C,0x50,0x50,0x50,0x3C,0x00 }, // 'y'
  { 0x44,0x64,0x54,0x4C,0x44,0x00 }, // 'z'
  { 0x00,0x08,0x36,0x41,0x00,0x00 }, // '{'
  { 0x00,0x00,0x7F,0x00,0x00,0x00 }, // '|'
  { 0x00,0x41,0x36,0x08,0x00,0x00 }, // '}'
  { 0x10,0x08,0x08,0x10,0x08,0x00 }, // '~'
  { 0x00,0x00,0x00,0x00,0x00,0x00 }  // (del)
};

// ---------------------------------------------------------------------------
// SPI speed/transaction + fast write primitives
// ---------------------------------------------------------------------------
static const SPISettings TGLIB_SPI(8000000, MSBFIRST, SPI_MODE0);

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__)
  #if (TGLIB_CS_PIN==10) && (TGLIB_DC_PIN==8) && (TGLIB_RST_PIN==9)
    #define TGLIB_FAST_PINS 1
    // D10=PB2, D9=PB1, D8=PB0
    #define CS_LOW()   (PORTB &= (uint8_t)~_BV(2))
    #define CS_HIGH()  (PORTB |=  _BV(2))
    #define DC_LOW()   (PORTB &= (uint8_t)~_BV(0))
    #define DC_HIGH()  (PORTB |=  _BV(0))
    #define RST_LOW()  (PORTB &= (uint8_t)~_BV(1))
    #define RST_HIGH() (PORTB |=  _BV(1))

    static inline void spiWrite(uint8_t b) {
      SPDR = b;
      while (!(SPSR & _BV(SPIF))) {}
    }
  #else
    #define TGLIB_FAST_PINS 0
  #endif
#else
  #define TGLIB_FAST_PINS 0
#endif

#if !TGLIB_FAST_PINS
  static inline void CS_LOW()   { digitalWrite(TGLIB_CS_PIN, LOW); }
  static inline void CS_HIGH()  { digitalWrite(TGLIB_CS_PIN, HIGH); }
  static inline void DC_LOW()   { digitalWrite(TGLIB_DC_PIN, LOW); }
  static inline void DC_HIGH()  { digitalWrite(TGLIB_DC_PIN, HIGH); }
  static inline void RST_LOW()  { digitalWrite(TGLIB_RST_PIN, LOW); }
  static inline void RST_HIGH() { digitalWrite(TGLIB_RST_PIN, HIGH); }

  static inline void spiWrite(uint8_t b) { SPI.transfer(b); }
#endif

static inline void tglib_writeBegin() {
  SPI.beginTransaction(TGLIB_SPI);
  CS_LOW();
}
static inline void tglib_writeEnd() {
  CS_HIGH();
  SPI.endTransaction();
}
static inline void _tglib_cmd(uint8_t c) {
  DC_LOW();
  spiWrite(c);
  DC_HIGH();
}
static inline void _tglib_u8(uint8_t d) {
  spiWrite(d);
}
static inline void _tglib_u16(uint16_t v) {
  spiWrite((uint8_t)(v >> 8));
  spiWrite((uint8_t)v);
}

// ---------------------------------------------------------------------------
// Low-level compatibility functions (kept for API compatibility)
// ---------------------------------------------------------------------------
void tglib_Data(uint8_t d) {
  tglib_writeBegin();
  _tglib_u8(d);
  tglib_writeEnd();
}

void tglib_Command(uint8_t c) {
  tglib_writeBegin();
  _tglib_cmd(c);
  tglib_writeEnd();
}

void tglib_Command2(uint8_t c, uint16_t d1, uint16_t d2) {
  tglib_writeBegin();
  _tglib_cmd(c);
  _tglib_u16(d1);
  _tglib_u16(d2);
  tglib_writeEnd();
}

// ---------------------------------------------------------------------------
// Init / power
// ---------------------------------------------------------------------------
void tglib_InitDisplay() {
  pinMode(tglib_dc, OUTPUT);
  pinMode(tglib_cs, OUTPUT);
  pinMode(tglib_rst, OUTPUT);

  CS_HIGH();
  DC_HIGH();

  SPI.begin();

  // Optional hard reset
  RST_HIGH();
  delay(5);
  RST_LOW();
  delay(20);
  RST_HIGH();
  delay(120);

  // Software reset
  tglib_Command(0x01);
  delay(120);

  // MADCTL: rotation + RGB/BGR
  // Your original logic: (rotate<<5) | (bgr<<3)
  tglib_writeBegin();
  _tglib_cmd(0x36);
  _tglib_u8((uint8_t)((tglib_rotate << 5) | (tglib_bgr << 3)));
  tglib_writeEnd();

  // Color mode: 16-bit (0x55)
  tglib_writeBegin();
  _tglib_cmd(0x3A);
  _tglib_u8(0x55);
  tglib_writeEnd();

  // Inversion
  tglib_Command((uint8_t)(0x20 + tglib_invert));

  // Sleep out
  tglib_Command(0x11);
  delay(120);
}

void tglib_DisplayOn() {
  tglib_Command(0x29);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
unsigned int tglib_Colour(int r, int g, int b) {
  // RGB888 -> RGB565
  return (unsigned int)(
    ((r & 0xF8) << 8) |
    ((g & 0xFC) << 3) |
    ((b & 0xF8) >> 3)
  );
}

void tglib_MoveTo(int x, int y) {
  tglib_xpos = x;
  tglib_ypos = y;
}

// ---------------------------------------------------------------------------
// Fast primitives (internal)
// NOTE: Coordinate convention preserved from your original code:
//   CASET uses (y), RASET uses (x)
// ---------------------------------------------------------------------------
static inline void tglib_setWindow_xy(int x0, int y0, int x1, int y1) {
  // CASET = "column" but in this library we feed it Y
  _tglib_cmd((uint8_t)tglib_CASET);
  _tglib_u16((uint16_t)(tglib_yoff + y0));
  _tglib_u16((uint16_t)(tglib_yoff + y1));

  // RASET = "row" but in this library we feed it X
  _tglib_cmd((uint8_t)tglib_RASET);
  _tglib_u16((uint16_t)(tglib_xoff + x0));
  _tglib_u16((uint16_t)(tglib_xoff + x1));

  _tglib_cmd((uint8_t)tglib_RAMWR);
}

static void tglib_DrawFastHLine(int x0, int y, int x1) {
  if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
  uint32_t n = (uint32_t)(x1 - x0 + 1);
  uint8_t hi = (uint8_t)(tglib_fore >> 8);
  uint8_t lo = (uint8_t)(tglib_fore & 0xFF);

  tglib_writeBegin();
  tglib_setWindow_xy(x0, y, x1, y);
  while (n--) { spiWrite(hi); spiWrite(lo); }
  tglib_writeEnd();
}

static void tglib_DrawFastVLine(int x, int y0, int y1) {
  if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
  uint32_t n = (uint32_t)(y1 - y0 + 1);
  uint8_t hi = (uint8_t)(tglib_fore >> 8);
  uint8_t lo = (uint8_t)(tglib_fore & 0xFF);

  tglib_writeBegin();
  tglib_setWindow_xy(x, y0, x, y1);
  while (n--) { spiWrite(hi); spiWrite(lo); }
  tglib_writeEnd();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void tglib_ClearDisplay() {
  // Clears to tglib_back (previous version always wrote zeros; default is black anyway).
  uint32_t n = (uint32_t)tglib_xsize * (uint32_t)tglib_ysize;
  uint8_t hi = (uint8_t)(tglib_back >> 8);
  uint8_t lo = (uint8_t)(tglib_back & 0xFF);

  tglib_writeBegin();
  tglib_setWindow_xy(0, 0, tglib_xsize - 1, tglib_ysize - 1);
  while (n--) { spiWrite(hi); spiWrite(lo); }
  tglib_writeEnd();
}

void tglib_PlotPoint(int x, int y) {
  uint8_t hi = (uint8_t)(tglib_fore >> 8);
  uint8_t lo = (uint8_t)(tglib_fore & 0xFF);

  tglib_writeBegin();
  tglib_setWindow_xy(x, y, x, y);
  spiWrite(hi);
  spiWrite(lo);
  tglib_writeEnd();
}

void tglib_FillRect(int w, int h) {
  if (w <= 0 || h <= 0) return;

  uint32_t n = (uint32_t)w * (uint32_t)h;
  uint8_t hi = (uint8_t)(tglib_fore >> 8);
  uint8_t lo = (uint8_t)(tglib_fore & 0xFF);

  tglib_writeBegin();
  tglib_setWindow_xy(tglib_xpos, tglib_ypos, tglib_xpos + w - 1, tglib_ypos + h - 1);
  while (n--) { spiWrite(hi); spiWrite(lo); }
  tglib_writeEnd();
}

// Bresenham line (diagonals still expensive; fast paths handle H/V)
void tglib_DrawTo(int x, int y) {
  // Fast paths for common UI primitives
  if (y == tglib_ypos) { tglib_DrawFastHLine(tglib_xpos, y, x); tglib_xpos = x; return; }
  if (x == tglib_xpos) { tglib_DrawFastVLine(x, tglib_ypos, y); tglib_ypos = y; return; }

  int sx, sy, e2, err;
  int dx = abs(x - tglib_xpos);
  int dy = abs(y - tglib_ypos);
  sx = (tglib_xpos < x) ? 1 : -1;
  sy = (tglib_ypos < y) ? 1 : -1;
  err = dx - dy;

  for (;;) {
    tglib_PlotPoint(tglib_xpos, tglib_ypos);
    if (tglib_xpos == x && tglib_ypos == y) break;
    e2 = err << 1;
    if (e2 > -dy) { err -= dy; tglib_xpos += sx; }
    if (e2 <  dx) { err += dx; tglib_ypos += sy; }
  }
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
void tglib_PlotChar(char c) {
  if ((uint8_t)c < 32) c = '?';
  if ((uint8_t)c > 127) c = '?';

  const uint8_t fhi = (uint8_t)(tglib_fore >> 8);
  const uint8_t flo = (uint8_t)(tglib_fore & 0xFF);
  const uint8_t bhi = (uint8_t)(tglib_back >> 8);
  const uint8_t blo = (uint8_t)(tglib_back & 0xFF);

  int x0 = tglib_xpos;
  int y0 = tglib_ypos;
  int x1 = x0 + 6 * tglib_scale - 1;
  int y1 = y0 + 8 * tglib_scale - 1;

  tglib_writeBegin();
  tglib_setWindow_xy(x0, y0, x1, y1);

  uint8_t idx = (uint8_t)c - 32;
  for (int xx = 0; xx < 6; xx++) {
    uint8_t bits = pgm_read_byte(&tglib_CharMap[idx][xx]);
    for (int xr = 0; xr < tglib_scale; xr++) {
      for (int yy = 0; yy < 8; yy++) {
        bool on = ((bits >> (7 - yy)) & 1) != 0;
        uint8_t hi = on ? fhi : bhi;
        uint8_t lo = on ? flo : blo;
        for (int yr = 0; yr < tglib_scale; yr++) {
          spiWrite(hi);
          spiWrite(lo);
        }
      }
    }
  }

  tglib_writeEnd();
  tglib_xpos += 6 * tglib_scale;
}

void tglib_PlotText(PGM_P s) {
  while (true) {
    char c = (char)pgm_read_byte(s++);
    if (c == 0) break;
    if (c == '\n') {
      tglib_xpos = 0;
      tglib_ypos += 8 * tglib_scale;
      continue;
    }
    tglib_PlotChar(c);
  }
}

void tglib_PlotTextRam(const char *s) {
  while (*s) {
    char c = *s++;
    if (c == '\n') {
      tglib_xpos = 0;
      tglib_ypos += 8 * tglib_scale;
      continue;
    }
    tglib_PlotChar(c);
  }
}

void tglib_PlotInt(int n) {
  char buf[12];
  itoa(n, buf, 10);
  tglib_PlotTextRam(buf);
}

// ---------------------------------------------------------------------------
// Library init (kept compatible with your existing call pattern)
// ---------------------------------------------------------------------------
void tglib_init() {
  tglib_InitDisplay();
  tglib_ClearDisplay();
  tglib_DisplayOn();
  tglib_MoveTo(0, 0);
}

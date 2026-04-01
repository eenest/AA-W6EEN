
#include <SPI.h>
#include <EEPROM.h>
#include "tglib.h"
#include "swr-pico.h"

#define BOOT_ZEROLEVEL_CAL 0   // set to 0 for normal firmware


#define COLOR_MENU_BG        C_BLACK
#define COLOR_MENU_TEXT      C_WHITE
#define COLOR_MENU_ACTIVE_TX C_YELLOW
#define COLOR_MENU_EDIT_BG   C_BLUE



// --- Forward declare Screen so Arduino auto-prototypes don't break ---
enum Screen : uint8_t;

// ================== Firmware version ==================
static const uint8_t FW_VER_MAJOR = 0;
static const uint8_t FW_VER_MINOR = 9;   // 0.08

// ================== PINS (per SSOT, locked) ==================
//#define TFT_CS    7
//#define TFT_DC    8
//#define TFT_RST   9

//#define TFT_CS    10
//#define TFT_DC    8
//#define TFT_RST   9


#define ENC_A     2
#define ENC_B     3
#define ENC_SW    4

#define PIN_PWR_HOLD       5
#define PIN_PWR_BTN_SENSE  6
#define PIN_BATT_SENSE     A2

// ================== TFT ==================



// ================== Font metrics (GFX default font) ==================
#define FONT_W   6   // px per character (monospaced)
#define FONT_H   8   // px

// ================== Menu layout & colors (SSOT) ==================
#define MENU_X0      4
#define MENU_W       (tft_width() - 8)
#define MENU_VALUE_X  (MENU_LABEL_X + 17 * FONT_W)   // fixed value column; avoids overlap with "Freq. Display"
#define MENU_ARROW_X  12                        // where ">" is drawn
#define MENU_LABEL_X  (MENU_ARROW_X + FONT_W)   // shift labels 1 char right


// ================== Top bar geometry ==================
static const int16_t TOPBAR_Y = 0;
static const int16_t TOPBAR_H = 19;
static const int16_t TB_TEXT_Y = TOPBAR_Y + 5;

static const int16_t TOPBAR_GAP = 1;   // SSOT rev13/18: 1px gap between top bar and graph work area

// ================== Graph geometry (SSOT rev13 Section 18) ==================
static const int16_t GRAPH_W = 300;    // inner graph area width (px)
static const int16_t GRAPH_H = 200;    // inner graph area height (px)
static const int16_t NOTCH_W = (FONT_W / 2);
static const int16_t NOTCH_H = (FONT_W / 2);

static const uint16_t CURSOR_INVALID = 0xFFFF;

// Packed SWR buffer: 10 bits/sample, 0..10.23 (centi-SWR), 0=invalid
#define PACK10_MAX_X100 1023

#define PACK10_N GRAPH_W   // enforce match
#include "pack10ram.h"  // packed SWR curve buffer (replaces swrBuf[])

static_assert(PACK10_N == GRAPH_W, "PACK10_N must match GRAPH_W");

static bool     graphHasData = false;
static bool sweepInProgress = false;

// Cached inner-graph dimensions actually used for drawing (in case GW/GH ever clamp)

static int16_t gGX0 = 0, gGY0 = 0, gGY1 = 0;
static int16_t gX_AXIS = 0, gY_AXIS = 0;
static int16_t gGX1 = 0;
static int16_t gGW = 0, gGH = 0;   // cached inner graph width/height (may be <= GRAPH_W/H if clamped)
static bool    gGraphGeomValid = false;

// ---- character-based layout ----
// column indices (NOT pixels)
#define COL_SWR_LABEL   0   // "SWR="
#define COL_SWR_NOW     4   // current
#define COL_LBRACKET    10   // [
#define COL_SWR_MIN     11
#define COL_DASH        16
#define COL_SWR_MAX     18
#define COL_RBRACKET    23  // ]
#define COL_FREQ_LABEL  25  // "F="
#define COL_FREQ_VAL    27
#define COL_MHZ         33
#define COL_BAND_VAL    35

// pixel X
#define X(col) ((col) * FONT_W)

// ================== UI layout ==================
static const int16_t MENU_Y0 = TOPBAR_Y + TOPBAR_H + 6;  // ≈ 25 px
static const int16_t MENU_DY = 18;
static const int16_t HELP_Y  = 240 - 16;  // bottom help line (rotation=3)

// ================== Battery icon geometry ==================
static const int16_t BATT_BODY_W = 22;
static const int16_t BATT_BODY_H = 12;
static const int16_t BATT_NUB_W  = 3;
static const int16_t BATT_NUB_H  = 6;
static const int16_t BATT_ICON_W = (BATT_BODY_W + BATT_NUB_W + 2);
static const int16_t BATT_ICON_H = (BATT_BODY_H + 2);
static const int16_t BATT_MARGIN_RIGHT = 2;

// SSOT 6.1.2: reserved top-bar width for battery (derived; no magic numbers)
static const int16_t TOPBAR_BATT_W = (BATT_ICON_W + BATT_MARGIN_RIGHT + 2);

// right limit before battery icon
static const int16_t X_TEXT_RIGHT_LIMIT = 240 - BATT_ICON_W - BATT_MARGIN_RIGHT - 2;

// ================== Bands ==================
enum BandId : uint8_t {
  BAND_160M = 0,
  BAND_80M,
  BAND_60M,
  BAND_40M,
  BAND_30M,
  BAND_20M,
  BAND_17M,
  BAND_15M,
  BAND_12M,
  BAND_10M,
  BAND_6M,
  BAND_USER,          // must be last
  BAND_COUNT
};

static const char* bandName[BAND_COUNT] = {
  "160m",
  "80m ",
  "60m ",
  "40m ",
  "30m ",
  "20m ",
  "17m ",
  "15m ",
  "12m ",
  "10m ",
  "6m  ",
  "USER"
};

// FCC amateur band edges (Hz) — SSOT
// Source: FCC Part 97
struct BandRange {
  uint32_t fLo;
  uint32_t fHi;
};

static const BandRange bandRange[BAND_COUNT] = {
  { 1800000UL,  2000000UL },  // 160m
  { 3500000UL,  4000000UL },  // 80m
  { 5330500UL,  5403500UL },  // 60m (5 channels, generalized span)
  { 7000000UL,  7300000UL },  // 40m
  {10100000UL, 10150000UL },  // 30m
  {14000000UL, 14350000UL },  // 20m
  {18068000UL, 18168000UL },  // 17m
  {21000000UL, 21450000UL },  // 15m
  {24890000UL, 24990000UL },  // 12m
  {28000000UL, 29700000UL },  // 10m
  {50000000UL, 54000000UL },  // 6m
  {        0,          0 }    // USER
};
// ================== USER band runtime range (Hz) ==================
// Default: 1–60 MHz. When/if user edits these later, persist to EEPROM.
static uint32_t userBandLoHz = 1000000UL;
static uint32_t userBandHiHz = 60000000UL;

// ================== Measurement / Graph state ==================

static uint16_t swrCurrent_x100  = 100;   // current displayed SWR
static uint16_t swrMinCur_x100   = 100;   // current window/min for UI
static uint16_t swrMaxCur_x100   = 100;   // current window/max for UI

static uint32_t cursorHz = 7000000UL;   // 7.000 MHz
static BandId currentBand = BAND_40M;

enum TBUpdateFlags : uint8_t {
  TB_UPD_NONE      = 0,
  TB_UPD_SWR_CURR  = 1 << 0,
  TB_UPD_SWR_RANGE = 1 << 1,
  TB_UPD_FREQ      = 1 << 2,
  TB_UPD_BAND      = 1 << 3,
  TB_UPD_ALL       = TB_UPD_SWR_CURR | TB_UPD_SWR_RANGE | TB_UPD_FREQ | TB_UPD_BAND,
  TB_FORCE         = 1 << 7
};

// last-drawn caches (to avoid redraw)
static uint16_t last_swrCurrent_x100 = 0xFFFF;
static uint16_t last_swrMinCur_x100  = 0xFFFF;
static uint16_t last_swrMaxCur_x100  = 0xFFFF;
static uint32_t last_cursorHz        = 0xFFFFFFFF;
static BandId   last_band            = (BandId)0xFF;
static uint32_t last_userLoHz        = 0xFFFFFFFF;
static uint32_t last_userHiHz        = 0xFFFFFFFF;

// Per-band cursor index (0..GRAPH_W-1). CURSOR_INVALID means “unset”.
static uint16_t cursorX_byBand[BAND_COUNT];

// ================== Encoder FSM ==================
#define DIR_NONE 0x00
#define DIR_CW   0x10
#define DIR_CCW  0x20
#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6

static const uint8_t ttable[7][4] = {
  {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
  {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
  {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
  {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
  {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
  {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
  {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};

static int8_t  encDetentDelta = 0;
static uint8_t encState = R_START;
static uint8_t encLastAB = 0;
static uint32_t encPollDueMs = 0;

// ================== Button / click ==================
bool btnStable = true;
bool btnLastStable = true;
uint32_t btnLastChangeMs = 0;
const uint16_t BTN_DEBOUNCE_MS = 20;

// --- long press ---
//static uint32_t btnPressStartMs = 0;
static const uint16_t BTN_LONGPRESS_MS = 500;  // 500 ms

// ================== Soft-latch ==================
static const uint32_t SHUTDOWN_HOLD_MS = 3000;
static bool shutdownArmed = false;
static bool shutdownMsgShown = false;
static uint32_t k0LowSinceMs = 0;

// ================== Battery ==================
static const float BATT_DIV_RATIO = 100000.0f / (330000.0f + 100000.0f);
static const float ADC_VREF_INTERNAL = 1.1f;
static uint32_t battSampleDueMs = 0;
static float battVoltsEma = 0.0f;
static float battVoltsUncal = 0.0f;
static bool battInit = false;

// Battery UI thresholds
static const float BATT_FULL_V      = 4.20f;  // full icon
static const float BATT_CHARGE_ON_V = 4.22f;  // "charging" detect threshold (tune)
static const float BATT_CHARGE_HYS  = 0.05f;  // hysteresis (tune)

static bool battChargingUi = false;           // sticky state for UI
static bool lastBattChargingShown = false;    // cache for redraw decisions



static bool calUiDirty = false;

static int   battRawLast = 0;
static float battVadcLast = 0.0f;

// ---- Quartz Frequency (Hz) ----
#define XTAL_MIN 24000000UL
#define XTAL_MAX 35000000UL

// Default per SSOT: 25 MHz
static const uint32_t QTZ_DEFAULT_HZ = 25002220UL;

static uint32_t quartzHz = QTZ_DEFAULT_HZ;
static uint32_t quartzHzOrig = QTZ_DEFAULT_HZ;   // for Cancel

// ================== EEPROM ==================
struct EepromSettings {
  uint16_t magic;
  uint8_t  eeversion;
  uint8_t  fw_major;
  uint8_t  fw_minor;

  uint8_t  current_band;   // BandId
  uint8_t  swr_range_sel;   // 0..SWR_RANGE_N-1 (SWR Range selection)
  
  uint8_t  auto_rescan_sel; // 0..AR_N-1 (Auto rescan selection)

  uint16_t cursor_x[BAND_COUNT];  // per-band cursor index (0..GRAPH_W-1), or CURSOR_INVALID

  float    batt_scale;

  uint32_t user_lo_hz;      // USER band low edge (Hz)
  uint32_t user_hi_hz;      // USER band high edge (Hz)
  
  uint32_t quartz_hz;      // Quartz frequency (Hz)

};

static const uint16_t EEPROM_MAGIC = 0xAACE;
static const uint8_t  EEPROM_VER   = 6;
static const int EEPROM_ADDR = 0;
static float battCalScale = 1.0f;

// ================== Screens ==================
enum Screen : uint8_t {
  SCR_GRAPH = 0,
  SCR_MAIN_MENU,
  SCR_SYSTEM_INFO,
  SCR_RESET_CONFIRM,

  // SSOT rev12 screens
  SCR_USER_BAND_SETTINGS,
  
  SCR_XTAL_FREQ,
  SCR_CALIBRATION,
  
  SCR_NONE = 255
};

static Screen screen = SCR_GRAPH;

// ================== Menu FSM ==================
// SSOT 17.7: Allowed values: "1..10", "1..5", "1..2.5"
static const char* const swrRangeValues[] = { "1..10", "1..5", "1..2.5" };
static const uint8_t SWR_RANGE_N = sizeof(swrRangeValues) / sizeof(swrRangeValues[0]);
static uint8_t swrRangeSel = 0;          // current selection

// ================== Auto Rescan (SSOT rev24) ==================
// Menu options: None / 10 sec / 30 sec
enum AutoRescanSel : uint8_t {
  AR_NONE = 0,
  AR_10S  = 1,
  AR_30S  = 2,
  AR_N
};

static const char* const autoRescanValues[] = { "None", "10 sec", "30 sec" };
static AutoRescanSel autoRescanSel = AR_NONE;   // persisted

static uint32_t autoNextDueMs = 0;        // when the next auto sweep is due (millis)
static bool firstGraphEntryAfterBoot = true;
static bool graphDataInvalid = true;  // true means: old curve must be cleared/ignored

static inline void invalidateGraphData() {
  graphDataInvalid = true;

  // Old curve samples are not valid anymore
  pack10_clear();
  graphHasData = false;

  // Reset UI min/max to safe defaults
  swrMinCur_x100 = 100;   // 1.00
  swrMaxCur_x100 = 100;   // 1.00
  swrCurrent_x100 = 100;  // optional: also reset current display
}

static inline uint32_t autoIntervalMs() {
  if (autoRescanSel == AR_10S) return 10000UL;
  if (autoRescanSel == AR_30S) return 30000UL;
  return 0UL;
}

// Optional: require calibration to run auto sweeps (recommended)
static inline bool isCalibratedNow() {
  bool zlValid = false;
  uint8_t zlCount = 0;
  zeroLevel_get_state(&zlValid, &zlCount);
  return zlValid;
}

static inline void autoScheduleFromNowFullInterval() {
  const uint32_t iv = autoIntervalMs();
  autoNextDueMs = iv ? (millis() + iv) : 0;
}


enum MenuItemType : uint8_t {
  MI_ACTION,
  MI_FIELD
};

struct MenuItemEx {
  const char* label;
  MenuItemType type;

  // action
  Screen nextScreen;

  // field
  int8_t* value;
  int8_t  minVal;
  int8_t  maxVal;
};

// Main menu row indices (keep in sync with SSOT rev20)
enum MainRow : uint8_t {
  MR_BAND = 0,
  MR_USER_BAND,
  MR_SWR_RANGE,
  MR_AUTO_RESCAN,
  MR_BATT_CAL,
  MR_QUARTZ,
  MR_CALIBRATION,
  MR_RESET,
  MR_SYSINFO,
  MR_BACK,
  MR_COUNT
};

static MenuItemEx mainMenuEx[] = {
  // 1) Band <name> <start>..<end> MHz (editable)
  { "Band",            MI_FIELD,  SCR_NONE,              (int8_t*)&currentBand,  0, (int8_t)(BAND_COUNT - 1) },

  // 2) User Band <start>-<end> MHz (submenu)
  { "User Band",       MI_ACTION, SCR_USER_BAND_SETTINGS, nullptr,               0, 0 },

  // 3) SWR Range <value> (editable)
  { "SWR Range",       MI_FIELD,  SCR_NONE,              (int8_t*)&swrRangeSel,  0, (int8_t)(SWR_RANGE_N - 1) },

  // 4) Auto rescan (editable)
  { "Auto rescan",     MI_FIELD,  SCR_NONE,              (int8_t*)&autoRescanSel, 0, (int8_t)(AR_N - 1) },

  // 5) Battery V Cal. <value> V (editable; special handling in code)
  { "Battery V Cal.",  MI_ACTION, SCR_NONE,              nullptr,               0, 0 },

  // 6) Quartz Frequency <value> (editable; special handling in code)
  { "Quartz Frequency",MI_ACTION, SCR_XTAL_FREQ,         nullptr,               0, 0 },

  // 7) Calibration (ZeroLevel array in EEPROM)
  { "Calibration",     MI_ACTION, SCR_CALIBRATION,       nullptr,               0, 0 },

  // 8) Reset to Defaults (action -> confirm screen)
  { "Reset to Defaults",MI_ACTION, SCR_RESET_CONFIRM,     nullptr,               0, 0 },

  // 9) System Info
  { "System Info",     MI_ACTION, SCR_SYSTEM_INFO,       nullptr,               0, 0 },

  // 10) Back
  { "Back",            MI_ACTION, SCR_GRAPH,             nullptr,               0, 0 }
};

static const uint8_t MAIN_EX_N = MR_COUNT;
static_assert(MAIN_EX_N == (sizeof(mainMenuEx) / sizeof(mainMenuEx[0])),
              "mainMenuEx[] size mismatch");

static bool menuEditMode = false;
static int8_t menuEditOrig = 0;

static bool bandDirty = false;   // set when Band changes during edit; used to decide whether Save writes EEPROM

// ================== Menus (data-driven) ==================



static int8_t menuSel = 0;


// Originals for Cancel behavior
//static uint8_t swrRangeSelOrig = 0;

// ---- User Band submenu (SSOT 17.6) ----
enum UserBandRow : uint8_t {
  UB_START = 0,
  UB_STOP  = 1,
  UB_STEP  = 2,
  UB_RESET = 3,
  UB_BACK  = 4
};

enum UserBandEditField : uint8_t {
  UB_EDIT_START = 0,
  UB_EDIT_STOP  = 1,
  UB_EDIT_STEP  = 2,
  UB_EDIT_NONE  = 255
};

static const char* userBandItems[] = {
  "Start Frequency",
  "Stop Frequency",
  "Step",
  "Reset Cursor to Center",
  "Back"
};

static const uint8_t UB_N = sizeof(userBandItems) / sizeof(userBandItems[0]);
static_assert(UB_N == 5, "userBandItems[] size changed; update UserBandRow enum");
static_assert(UB_BACK == 4, "UserBandRow enum changed; keep it aligned to userBandItems[]");

static int8_t ubSel = 0;

// ---- User Band edit (SSOT 17.6) ----
static const uint32_t UB_MIN_HZ = 1000000UL;    // 1 MHz
static const uint32_t UB_MAX_HZ = 60000000UL;   // 60 MHz

// Step list (RAM-only)
static const uint32_t UB_STEP_LIST_HZ[] = { 10000UL, 100000UL, 1000000UL }; // 10k, 100k, 1M
static const uint8_t  UB_STEP_LIST_N    = sizeof(UB_STEP_LIST_HZ) / sizeof(UB_STEP_LIST_HZ[0]);

static uint8_t ubStepSel     = 1;  // default 100 kHz (SSOT)
static uint8_t ubStepOrigSel = 1;  // for Cancel

static inline uint32_t ubStepHz() {
  return UB_STEP_LIST_HZ[ubStepSel];
}

static bool ubEditMode = false;
static UserBandEditField ubEditItem = UB_EDIT_NONE;

static uint32_t ubLoOrigHz = 0;
static uint32_t ubHiOrigHz = 0;

// ============================================================
// Quartz Frequency submenu (SCR_XTAL_FREQ) state (SSOT 17.11)
// - Menu selection + edit mode are RAM-only (not persisted)
// - Step selection is RAM-only and defaults to 1 Hz
// ============================================================

// Row enum for SCR_XTAL_FREQ menu
enum XtalRow : uint8_t {
  XTAL_FREQ = 0,
  XTAL_STEP = 1,
  XTAL_BACK = 2
};

// Selection within SCR_XTAL_FREQ
static int8_t xtalSel = 0;

// Edit mode within SCR_XTAL_FREQ
static bool xtalEditMode = false;

// Which item is being edited (only meaningful when xtalEditMode == true)
enum XtalEditField : uint8_t {
  XTAL_EDIT_FREQ = 0,
  XTAL_EDIT_STEP = 1,
  XTAL_EDIT_NONE = 255
};

static XtalEditField xtalEditItem = XTAL_EDIT_NONE;

// Step list (SSOT step options: 1 / 10 / 100 / 1000 / 10000 Hz)
// (Array itself is constant; selection is RAM-only.)
static const uint16_t XTAL_STEP_LIST_HZ[] = { 1u, 10u, 100u, 1000u, 10000u };
static const uint8_t  XTAL_STEP_N = sizeof(XTAL_STEP_LIST_HZ) / sizeof(XTAL_STEP_LIST_HZ[0]);
static_assert(XTAL_STEP_N == 5, "XTAL_STEP_LIST_HZ[] size changed; update UI/formatting if needed");

// Current step selection index (RAM-only), default 0 => 1 Hz
static uint8_t xtalStepSel = 0;      // 0 => 1 Hz

static uint8_t xtalStepOrigSel = 0;  // for Cancel

static inline uint32_t xtalStepHz() {
  return (uint32_t)XTAL_STEP_LIST_HZ[xtalStepSel];
}

// Quartz tuning output state (10 MHz on CLK0 while in SCR_XTAL_FREQ)
static bool     xtalTuneActive = false;
static uint32_t xtalPrevF0 = 0;
static uint32_t xtalPrevF1 = 0;

// Quartz calibration workflow:
// - We keep the Si5351 XTAL reference fixed at the saved value while editing
// - Encoder changes the *programmed* CLK0 frequency around 10 MHz
// - UI displays the *computed* XTAL candidate derived from the programmed freq
static const uint32_t XTAL_CAL_TARGET_HZ   = 10000000UL;
static const uint32_t XTAL_CAL_PROG_MIN_HZ =  9000000UL;
static const uint32_t XTAL_CAL_PROG_MAX_HZ = 11000000UL;

static uint32_t xtalCalBaseHz      = QTZ_DEFAULT_HZ;      // assumed xtal at edit start
static uint32_t xtalCalProgHz      = XTAL_CAL_TARGET_HZ;  // current programmed CLK0
static uint32_t xtalCalCandidateHz = QTZ_DEFAULT_HZ;      // computed xtal shown/saved


// Reset confirm
enum ResetChoice : uint8_t {
  RST_NO  = 0,
  RST_YES = 1
};

static const char* resetItems[] = { "No", "Yes" };
static const uint8_t RST_N = sizeof(resetItems) / sizeof(resetItems[0]);
static_assert(RST_N == 2, "resetItems[] size changed; update ResetChoice enum");
static_assert(RST_YES == 1, "ResetChoice enum changed; keep it aligned to resetItems[]");

static int8_t rstSel = 0;

// ---- Calibration screen (SCR_CALIBRATION) (SSOT 17.12) ----
enum CalRow : uint8_t {
  CAL_CANCEL = 0,
  CAL_START  = 1
};

static const char* calItems[] = { "Cancel", "Start Calibration" };
static const uint8_t CAL_N = sizeof(calItems) / sizeof(calItems[0]);
static_assert(CAL_N == 2, "calItems[] size changed; update CalRow enum");
static_assert(CAL_START == 1, "CalRow enum changed; keep it aligned to calItems[]");

static int8_t calSel = 0;

// SSOT 17.5 / 17.10 Battery calibration
static constexpr float BATT_CAL_V_MIN  = 3.00f;
static constexpr float BATT_CAL_V_MAX  = 4.25f;
static constexpr float BATT_CAL_STEP_V = 0.005f;  // internal step per detent (display is 2dp)

// Calibration edit
//static float calScaleWorking = 1.0f;
static float calTargetVolts = 0.0f; // user-adjusted displayed volts

static float battCalScaleOrig = 1.0f;   // for Cancel (revert)
static float battCalRawRefV   = 0.0f;   // frozen raw (uncal) reference captured when entering edit

// Battery calibration edit step
static const float CAL_VSTEP_PER_DETENT = 0.01f; // volts per encoder detent

// ================== System Info live-update ==================
static uint32_t sysInfoDueMs = 0;
static int16_t  lastSysBattMv = -1;
static uint8_t  lastSysBattPct = 255;

// layout for value boxes
static int16_t  sysInfoValX = 0;
static int16_t  sysInfoValW = 0;
static int16_t  sysInfoBattVy = 0;
static int16_t  sysInfoBattPctY = 0;


// ================== UI primitives (direct tglib) ==================
// --- coordinate mapping: aa-code uses top-left origin; tglib uses bottom-left origin ---
static inline int16_t _H() { return (int16_t)tglib_ysize; }

// map a single Y coordinate (pixel/line endpoints)
static inline int16_t _mapY_point(int16_t y) {
  return (int16_t)(_H() - 1 - y);
}

// map a rectangle/top-left anchored object of height h (fillRect, text boxes, etc.)
static inline int16_t _mapY_rect(int16_t y, int16_t h) {
  return (int16_t)(_H() - h - y);
}


static inline int16_t tft_width()  { return (int16_t)tglib_xsize; }
static inline int16_t tft_height() { return (int16_t)tglib_ysize; }

static inline void tft_setTextColor(uint16_t fg, uint16_t bg) {
  tglib_fore = fg;
  tglib_back = bg;
}

static inline void tft_setTextSize(uint8_t s) {
  if (s < 1) s = 1;
  tglib_scale = (int)s;
}

static inline void tft_setCursor(int16_t x, int16_t y) {
  const int16_t gh = (int16_t)(8 * tglib_scale);   // glyph height in pixels
  tglib_MoveTo((int)x, (int)_mapY_rect(y, gh));
}

static inline void tft_print_char(char c) {
  tglib_PlotChar(c);
}

static inline void tft_print_str(const char* s) {
  tglib_PlotTextRam(s);
}

static inline void tft_print_P(PGM_P s) {
  tglib_PlotText(s);
}

static inline void tft_print_int(int v) {
  tglib_PlotInt(v);
}

static inline void tft_fillScreen(uint16_t c) {
  tglib_back = c;
  tglib_ClearDisplay();
}

static inline void tft_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  tglib_fore = c;
  tglib_MoveTo((int)x, (int)_mapY_rect(y, h));
  tglib_FillRect((int)w, (int)h);
}

static inline void tft_drawPixel(int16_t x, int16_t y, uint16_t c) {
  tglib_fore = c;
  tglib_PlotPoint((int)x, (int)_mapY_point(y));
}

static inline void tft_drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
  tglib_fore = c;
  tglib_MoveTo((int)x0, (int)_mapY_point(y0));
  tglib_DrawTo((int)x1, (int)_mapY_point(y1));
}

static inline void tft_drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
  if (w <= 0) return;
  tft_drawLine(x, y, (int16_t)(x + w - 1), y, c);
}

static inline void tft_drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
  if (h <= 0) return;
  tft_drawLine(x, y, x, (int16_t)(y + h - 1), c);
}

static inline void tft_drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  int16_t x1 = x + w - 1;
  int16_t y1 = y + h - 1;
  tft_drawLine(x,  y,  x1, y,  c);
  tft_drawLine(x1, y,  x1, y1, c);
  tft_drawLine(x1, y1, x,  y1, c);
  tft_drawLine(x,  y1, x,  y,  c);
}

// ================== Forward declarations (REQUIRED) ==================

// UI / menus

static void drawMenuItem(int16_t y, const char* text, bool selected);
static void drawResetConfirmAll();
static void drawCalibrationAll();
static void showCalibrationNeededPopup();

static void redrawMainMenuRow(uint8_t i, bool selected);

// Graph / measurement
static void drawGraphScreen();
static void runMeasurementStub();

// Battery
static void drawBatteryTopBarIfNeeded();
static void drawBatteryIcon(int16_t x, int16_t y, uint8_t pct, bool charging=false);
static uint8_t batteryPercentFromVolts(float v);
static float getBatteryVolts();

// Soft-latch
static void initSoftLatchPins();
static void serviceSoftLatch();
static void cutPowerNow();
static void drawShutdownOverlayOnce();

// Battery sense
static void initBatterySense();
static void serviceBatterySense();

// EEPROM
static bool eepromIsValid();
static void eepromLoad();
static void eepromSave();
//static void eepromInvalidateMagic();

// Screens / UI logic
static void enterScreen(Screen s);
static void handleDetents(int16_t detents);
static void handleClick(bool isLongPress);

// System info
static void drawSystemInfoStatic();
static void updateSystemInfoBatteryBox(bool force);
static void serviceSystemInfoLive();

// Encoder
static inline void encoderPoll();

// Header / menu
static void drawTopBar(PGM_P titleOrNull);

// misc
static void getBandEdgesHz(BandId b, uint32_t& lo, uint32_t& hi);
static void fmtVolt2dp(char* out, float v);
static void clearMenuExtraArea(uint8_t rowCount); 
static void fmtScale5dp(char* out, float k);
static void fmtFreq(char* buf, uint32_t hz, uint8_t decimals);
static void clearPlotAreaOnly();

// ================== Menu drawing helpers ==================
// Menu layout constants
static void drawMenuRow(
  int16_t y,
  const char* label,
  const char* value,     // may be nullptr
  bool selected,
  bool editing
) {
  const uint16_t bg =
    (selected && editing) ? COLOR_MENU_EDIT_BG : COLOR_MENU_BG;

  tft_fillRect(MENU_X0, y - 2, MENU_W, 16, bg);

  tft_setTextSize(1);
  tft_setTextColor(
    selected ? COLOR_MENU_ACTIVE_TX : COLOR_MENU_TEXT,
    bg
  );

  tft_setCursor(MENU_ARROW_X, y);
  tft_print_char(selected ? '>' : ' ');

  tft_setCursor(MENU_LABEL_X, y);
  tft_print_str(label);

  if (value && value[0]) {
    tft_setCursor(MENU_VALUE_X, y);
    tft_print_str(value);   // ✅ ИСПРАВЛЕНО
  }
}

static void drawHelpLine(PGM_P text) {
  // Clear help line area
  tft_fillRect(0, HELP_Y - 2, tft_width(), 16, C_BLACK);

  if (!text || !text[0]) return;

  tft_setTextSize(1);
  tft_setTextColor(C_WHITE, C_BLACK);
  tft_setCursor(4, HELP_Y);
  tft_print_P(text);
}

static void drawHelpLine_None() {
  drawHelpLine(PSTR(""));
}

static void drawHelpLine2(PGM_P line1, PGM_P line2) {
  // Two-line help area: use the space above the existing help line.
  // Clear ~26 px tall band at bottom.
  const int16_t y0 = HELP_Y - 10;
  tft_fillRect(0, y0, tft_width(), 26, C_BLACK);

  tft_setTextSize(1);
  tft_setTextColor(C_WHITE, C_BLACK);

  if (line1 && line1[0]) {
    tft_setCursor(4, HELP_Y - 9);
    tft_print_P(line1);
  }
  if (line2 && line2[0]) {
    tft_setCursor(4, HELP_Y);
    tft_print_P(line2);
  }
}

static void drawHelpLine_Quartz() {
  drawHelpLine2(
    PSTR("Monitor CLK0 on ANT (counter)"),
    PSTR("Rotate to 10MHz; screen shows XTAL")
  );
}


static void updateMainMenuHelpLine() {
  drawHelpLine_None();
}

void drawMainMenuEx() {
  tft_fillScreen(C_BLACK);

  drawTopBar(PSTR("Menu"));

  for (uint8_t i = 0; i < MAIN_EX_N; i++) {
    redrawMainMenuRow(i, (i == (uint8_t)menuSel));
  }

  updateMainMenuHelpLine();
}


// Format band range into out[] as:
//  - normal bands:  X.XXX–Y.YYY
//  - 60m band:      X.XXXX–Y.YYYY
// No snprintf(), AVR-safe.
static void fmtBandRange(char* out, BandId band) {

  uint32_t startHz, stopHz;

  // ------------------------------------------------------------
  // Obtain band limits in Hz
  // ------------------------------------------------------------
  getBandEdgesHz(band, startHz, stopHz);

  char* p = out;

  // ------------------------------------------------------------
  // 60m special case: 4 decimal digits
  // ------------------------------------------------------------
  if (band == BAND_60M) {

    uint32_t sm = startHz / 1000000UL;
    uint32_t em = stopHz  / 1000000UL;

    uint32_t sf = (startHz / 100UL) % 10000UL;
    uint32_t ef = (stopHz  / 100UL) % 10000UL;

    // start MHz
    utoa(sm, p, 10);
    while (*p) p++;

    *p++ = '.';

    *p++ = '0' + (sf / 1000) % 10;
    *p++ = '0' + (sf / 100)  % 10;
    *p++ = '0' + (sf / 10)   % 10;
    *p++ = '0' + (sf % 10);

    *p++ = '-';

    // end MHz
    utoa(em, p, 10);
    while (*p) p++;

    *p++ = '.';

    *p++ = '0' + (ef / 1000) % 10;
    *p++ = '0' + (ef / 100)  % 10;
    *p++ = '0' + (ef / 10)   % 10;
    *p++ = '0' + (ef % 10);

    *p = '\0';
    return;
  }

  // ------------------------------------------------------------
  // All other bands: 3 decimal digits
  // ------------------------------------------------------------

  uint32_t sm = startHz / 1000000UL;
  uint32_t em = stopHz  / 1000000UL;

  uint32_t sf = (startHz / 1000UL) % 1000UL;
  uint32_t ef = (stopHz  / 1000UL) % 1000UL;

  // start MHz
  utoa(sm, p, 10);
  while (*p) p++;

  *p++ = '.';

  *p++ = '0' + (sf / 100) % 10;
  *p++ = '0' + (sf / 10)  % 10;
  *p++ = '0' + (sf % 10);

  *p++ = '-';

  // end MHz
  utoa(em, p, 10);
  while (*p) p++;

  *p++ = '.';

  *p++ = '0' + (ef / 100) % 10;
  *p++ = '0' + (ef / 10)  % 10;
  *p++ = '0' + (ef % 10);

  *p = '\0';
}

static void fmtUserBandRange(char* out) {
  uint32_t startHz, stopHz;

  // get USER band edges via existing mechanism
  getBandEdgesHz(BAND_USER, startHz, stopHz);

  char* p = out;

  // ---- start MHz ----
  char* q = utoa(startHz / 1000000UL, p, 10);
  while (*q) q++;
  p = q;

  *p++ = '.';

  uint32_t sf = (startHz / 1000UL) % 1000UL;
  *p++ = '0' + (sf / 100) % 10;
  *p++ = '0' + (sf / 10)  % 10;
  *p++ = '0' + (sf % 10);

  *p++ = '-';

  // ---- end MHz ----
  q = utoa(stopHz / 1000000UL, p, 10);
  while (*q) q++;
  p = q;

  *p++ = '.';

  uint32_t ef = (stopHz / 1000UL) % 1000UL;
  *p++ = '0' + (ef / 100) % 10;
  *p++ = '0' + (ef / 10)  % 10;
  *p++ = '0' + (ef % 10);

  *p = '\0';
}

static void redrawMainMenuRow(uint8_t i, bool selected) {
  const int16_t y = MENU_Y0 + (int16_t)i * MENU_DY;

  const MenuItemEx& it = mainMenuEx[i];
  const bool isField  = (it.type == MI_FIELD);

  // Battery + Quartz are MI_ACTION but still have edit mode (special handling)
  const bool isSpecialEditable = (i == MR_BATT_CAL ); /* Battery V Cal */
  const bool editing  = (selected && menuEditMode && (isField || isSpecialEditable));

  const char* value = nullptr;
  char valueBuf[40];

  // ============================================================
  // Menu item–specific value rendering (SSOT-driven)
  // ============================================================
  switch (i) {

    // ------------------------------------------------------------
    // 0: Band (editable)
    // ------------------------------------------------------------
    case MR_BAND: {
      char range[20];
      fmtBandRange(range, currentBand);

      char* p = valueBuf;

      // band name
      const char* s = (currentBand == BAND_USER) ? "USER" : bandName[currentBand];
      while (*s) *p++ = *s++;

      *p++ = ' ';
      *p++ = ' ';

      // band range
      s = range;
      while (*s) *p++ = *s++;

      *p++ = ' ';
      *p++ = 'M';
      *p++ = 'H';
      *p++ = 'z';
      *p   = '\0';

      value = valueBuf;
      break;
    }

    // ------------------------------------------------------------
    // 1: User Band (submenu entry)
    // ------------------------------------------------------------
    case MR_USER_BAND: {
      char range[20];
      fmtUserBandRange(range); // EEPROM-based helper: "x.xxx-y.yyy"

      char* p = valueBuf;
      const char* s = range;
      while (*s) *p++ = *s++;

      *p++ = ' ';
      *p++ = 'M';
      *p++ = 'H';
      *p++ = 'z';
      *p   = '\0';

      value = valueBuf;
      break;
    }

    // ------------------------------------------------------------
    // 2: SWR Range (editable)
    // ------------------------------------------------------------
    case MR_SWR_RANGE:
      value = swrRangeValues[swrRangeSel];
      break;

    case MR_AUTO_RESCAN:  // Auto rescan
      value = autoRescanValues[autoRescanSel];
      break;

    // ------------------------------------------------------------
    // 4: Battery V Cal. (editable; special)
    // Show "xx.xx V"
    // - normal: current calibrated battery voltage
    // - editing: show calTargetVolts
    // ------------------------------------------------------------
    case MR_BATT_CAL: {
      const float v =
        (menuEditMode && selected) ? calTargetVolts : getBatteryVolts();

      char volts[8];
      fmtVolt2dp(volts, v); // e.g. "4.12"

      char* p = valueBuf;
      const char* s = volts;
      while (*s) *p++ = *s++;

      *p++ = ' ';
      *p++ = 'V';
      *p   = '\0';

      value = valueBuf;
      break;
    }

    // ------------------------------------------------------------
    // 5: Quartz Frequency (editable; special)
    // Show quartzHz in Hz (integer)
    // ------------------------------------------------------------
    case MR_QUARTZ:
      ultoa((unsigned long)quartzHz, valueBuf, 10);
      value = valueBuf;
      break;

    // ------------------------------------------------------------
    // All other rows: no value
    // ------------------------------------------------------------
    default:
      value = nullptr;
      break;
  }

  drawMenuRow(y, it.label, value, selected, editing);
}

static void drawMenuItem(int16_t y, const char* text, bool selected) {
  drawMenuRow(y, text, nullptr, selected, false);
}



static void drawBatteryCalPanel(uint8_t rowCount) {
  clearMenuExtraArea(rowCount);

  const int16_t y0 = MENU_Y0 + (int16_t)rowCount * MENU_DY + 6;

  tft_setTextSize(1);
  tft_setTextColor(C_WHITE, C_BLACK);

  // Raw (uncal) live
  tft_setCursor(6, y0 + 0);

  tft_print_P(PSTR("Raw: "));
  {
    char vbuf[8];
    fmtVolt2dp(vbuf, battVoltsUncal);
    tft_print_str(vbuf);
  }
  tft_print_P(PSTR(" V  "));

  tft_print_P(PSTR("K: "));
  {
    char kbuf[16];
    fmtScale5dp(kbuf, battCalScale);   // already exists in your code
    tft_print_str(kbuf);
  }

}

static void clearMenuExtraArea(uint8_t rowCount) {
  const int16_t y0 = MENU_Y0 + (int16_t)rowCount * MENU_DY + 4;
  const int16_t h  = (HELP_Y - 4) - y0;
  if (h > 0) tft_fillRect(0, y0, tft_width(), h, C_BLACK);
}


// Draw User Band submenu rows (with values for Start/Stop)
static void drawUserBandRow(uint8_t idx, bool selected) {
  const char* label = userBandItems[idx];
  const char* value = nullptr;

  // local buffers are OK: drawMenuRow prints immediately
  char fbuf[16];
  char vbuf[20];

  if (idx == UB_START || idx == UB_STOP) {
    // Start/Stop shown as MHz with 3 decimals
    fmtFreq(fbuf, (idx == UB_START) ? userBandLoHz : userBandHiHz, 3);

    // vbuf = fbuf + " MHz"  (no snprintf)
    uint8_t p = 0;
    for (const char* s = fbuf; *s && p < (uint8_t)(sizeof(vbuf) - 1); s++) vbuf[p++] = *s;
    const char* tail = " MHz";
    for (const char* s = tail; *s && p < (uint8_t)(sizeof(vbuf) - 1); s++) vbuf[p++] = *s;
    vbuf[p] = '\0';

    value = vbuf;
  }
  else if (idx == UB_STEP) {
    const char* s =
      (ubStepSel == 0) ? "10 kHz" :
      (ubStepSel == 1) ? "100 kHz" :
      (ubStepSel == 2) ? "1 MHz" :
                         "100 kHz";  // safety fallback

    uint8_t p = 0;
    for (; *s && p < (uint8_t)(sizeof(vbuf) - 1); s++) vbuf[p++] = *s;
    vbuf[p] = '\0';

    value = vbuf;
  }

  // editing highlight: ubEditItem uses same numeric values as rows (0/1/2)
  const bool editing = (selected && ubEditMode && (ubEditItem == idx));

  const int16_t y = MENU_Y0 + (int16_t)idx * MENU_DY;
  drawMenuRow(y, label, value, selected, editing);
}

// Draw Quartz Frequency submenu rows (SCR_XTAL_FREQ)
static void drawXtalRow(uint8_t idx, bool selected) {
  const char* label = nullptr;
  const char* value = nullptr;

  char vbuf[20];

  if (idx == XTAL_FREQ) {
    label = "Quartz Frequency";

    uint32_t shown = quartzHz;
    if (xtalEditMode && (xtalEditItem == XTAL_EDIT_FREQ)) {
      shown = xtalCalCandidateHz;   // computed candidate while editing
    }

    ultoa((unsigned long)shown, vbuf, 10);
    value = vbuf;
  } else if (idx == XTAL_STEP) {
    label = "Step";

    const char* s =
      (xtalStepSel == 0) ? "1 Hz" :
      (xtalStepSel == 1) ? "10 Hz" :
      (xtalStepSel == 2) ? "100 Hz" :
      (xtalStepSel == 3) ? "1 kHz" :
      (xtalStepSel == 4) ? "10 kHz" :
                           "1 Hz";

    uint8_t p = 0;
    for (; *s && p < (uint8_t)(sizeof(vbuf) - 1); s++) vbuf[p++] = *s;
    vbuf[p] = '\0';

    value = vbuf;
  }
  else {
    label = "Back";
    value = nullptr;
  }

  const bool editing =
    selected && xtalEditMode &&
    ((idx == XTAL_FREQ && xtalEditItem == XTAL_EDIT_FREQ) ||
     (idx == XTAL_STEP && xtalEditItem == XTAL_EDIT_STEP));

  const int16_t y = MENU_Y0 + (int16_t)idx * MENU_DY;
  drawMenuRow(y, label, value, selected, editing);
}


static void drawResetConfirmAll() {
  tft_fillScreen(C_BLACK);

  drawTopBar(PSTR("Menu->Reset Confirm"));

  tft_setTextSize(2);
  tft_setTextColor(C_WHITE, C_BLACK);
  tft_setCursor(10, 28);
  tft_print_P(PSTR("Reset defaults?"));

  tft_setTextSize(1);
  tft_setCursor(10, 60);
  tft_print_P(PSTR("Resets settings to defaults"));

  for (uint8_t i = 0; i < RST_N; i++) {
    int16_t y = 100 + (int16_t)i * 20;
    drawMenuItem(y, resetItems[i], (i == (uint8_t)rstSel));
  }
  drawHelpLine(PSTR("LP on YES: Reset   Click: Cancel"));
}

static void drawCalibrationAll() {
  tft_fillScreen(C_BLACK);

  drawTopBar(PSTR("Menu->Calibration"));

  tft_setTextSize(1);
  tft_setTextColor(C_WHITE, C_BLACK);

  // SSOT guidance text
  tft_setCursor(10, 30);
  tft_print_P(PSTR("Disconnect any load from"));
  tft_setCursor(10, 42);
  tft_print_P(PSTR("Antenna connector."));

  tft_setCursor(10, 60);
  tft_print_P(PSTR("Calibration runs 1..60 MHz"));
  tft_setCursor(10, 72);
  tft_print_P(PSTR("Takes about 2 seconds"));

  for (uint8_t i = 0; i < CAL_N; i++) {
    int16_t y = 100 + (int16_t)i * 20;
    drawMenuItem(y, calItems[i], (i == (uint8_t)calSel));
  }

  drawHelpLine(PSTR("LP on START: Run   Click on Cancel: Exit"));
}

static void showCalibrationNeededPopup() {
  if (screen != SCR_GRAPH) return;

  const int16_t W = (int16_t)tft_width();
  const int16_t H = (int16_t)tft_height();
  const int16_t workY0 = TOPBAR_Y + TOPBAR_H + TOPBAR_GAP;
  const int16_t workH = H - workY0;

  int16_t boxW = (int16_t)(W - 20);
  if (boxW > 220) boxW = 220;
  if (boxW < 140) boxW = (int16_t)(W - 10);

  const int16_t boxH = 44;
  const int16_t x = (int16_t)((W - boxW) / 2);
  const int16_t y = (int16_t)(workY0 + (workH - boxH) / 2);

  tft_fillRect(x, y, boxW, boxH, C_LIGHTGREY);
  tft_drawRect(x, y, boxW, boxH, C_WHITE);

  tft_setTextSize(1);
  tft_setTextColor(C_BLACK, C_LIGHTGREY);
  tft_setCursor(x + 6, y + 12);
  tft_print_P(PSTR("Calibration needed"));
  tft_setCursor(x + 6, y + 26);
  tft_print_P(PSTR("Menu -> Calibration"));

  delay(1200);
  drawGraphScreen();
}

static void printVoltage2dp(float v) {
  // Always print voltage with max 2 decimals, user-facing
  char buf[8];
  fmtVolt2dp(buf, v);   // already exists in your code
  tft_print_str(buf);

}

// ================== Top bar formatters ==================

static void fmtSWRx100(char* out, uint16_t v_x100) {
  // clamp to 99.99 (optional; adjust if you want >99.99)
  if (v_x100 > 9999) v_x100 = 9999;

  uint8_t i = (uint8_t)(v_x100 / 100);
  uint8_t f = (uint8_t)(v_x100 % 100);

  // "dd.dd" with leading space for <10
  out[0] = (i < 10) ? ' ' : (char)('0' + (i / 10));
  out[1] = (char)('0' + (i % 10));
  out[2] = '.';
  out[3] = (char)('0' + (f / 10));
  out[4] = (char)('0' + (f % 10));
  out[5] = 0;
}

// Format frequency (Hz) as MHz with `decimals` digits after '.'.
// Examples (no leading zero padding):
//   fmtFreq(buf, 7040000, 4)  -> "7.0400"
//   fmtFreq(buf, 7040000, 3)  -> "7.040"
// Rounding is done at the resolution implied by `decimals`.
static void fmtFreq(char* buf, uint32_t hz, uint8_t decimals) {
  if (decimals > 6) decimals = 6;

  // pow10[d] = 10^d
  static const uint32_t pow10[] = {
    1UL, 10UL, 100UL, 1000UL, 10000UL, 100000UL, 1000000UL
  };

  const uint32_t scale = pow10[decimals];          // 10^decimals
  const uint32_t div   = pow10[6 - decimals];      // 10^(6-decimals) Hz per 10^-decimals MHz
                                                   // decimals=3 -> div=1000 Hz
                                                   // decimals=4 -> div=100 Hz

  // Rounded scaled MHz value: units are 10^-decimals MHz
  uint32_t scaled = (hz + (div / 2UL)) / div;

  uint32_t mhz_int = scaled / scale;
  uint32_t frac    = scaled % scale;

  uint8_t p = 0;

  // Print integer part (no padding)
  // Handles 0..999 safely, but your range is typically 1..60 MHz.
  if (mhz_int >= 100) {
    buf[p++] = (char)('0' + (mhz_int / 100));
    mhz_int %= 100;
    buf[p++] = (char)('0' + (mhz_int / 10));
    buf[p++] = (char)('0' + (mhz_int % 10));
  } else if (mhz_int >= 10) {
    buf[p++] = (char)('0' + (mhz_int / 10));
    buf[p++] = (char)('0' + (mhz_int % 10));
  } else {
    buf[p++] = (char)('0' + mhz_int);
  }

  if (decimals) {
    buf[p++] = '.';

    // Print fractional part with leading zeros to exactly `decimals` digits
    for (uint8_t d = decimals; d > 0; d--) {
      uint32_t place = pow10[d - 1];
      uint8_t digit = (uint8_t)(frac / place);
      buf[p++] = (char)('0' + digit);
      frac %= place;
    }
  }

  buf[p] = '\0';
}

static void fmtVolt2dp(char* out, float v) {
  // Clamp not enforced here (caller decides); this just formats.
  if (v < 0) v = 0;

  uint16_t x = (uint16_t)(v * 100.0f + 0.5f); // 2dp rounding
  uint16_t w = x / 100;
  uint16_t f = x % 100;

  // write: w.f(2)
  char tmp[8];
  ultoa(w, tmp, 10);

  uint8_t p = 0;
  for (const char* s = tmp; *s; s++) out[p++] = *s;
  out[p++] = '.';
  out[p++] = (char)('0' + (f / 10));
  out[p++] = (char)('0' + (f % 10));
  out[p] = '\0';
}

static void fmtScale5dp(char* out, float k) {
  if (k < 0) k = 0;

  uint32_t x = (uint32_t)(k * 100000.0f + 0.5f); // 5dp rounding
  uint32_t w = x / 100000UL;
  uint32_t f = x % 100000UL;

  char tmp[12];
  ultoa(w, tmp, 10);

  uint8_t p = 0;
  for (const char* s = tmp; *s; s++) out[p++] = *s;
  out[p++] = '.';

  // pad 5 digits
  out[p++] = (char)('0' + (f / 10000UL) % 10);
  out[p++] = (char)('0' + (f /  1000UL) % 10);
  out[p++] = (char)('0' + (f /   100UL) % 10);
  out[p++] = (char)('0' + (f /    10UL) % 10);
  out[p++] = (char)('0' + (f /     1UL) % 10);
  out[p] = '\0';
}

static inline bool sweepAbortToMenuRequested(uint32_t& pressStartMs) {
  // active LOW button
  const bool pressed = (digitalRead(ENC_SW) == LOW);
  const uint32_t now = millis();

  if (!pressed) {
    pressStartMs = 0;
    return false;
  }

  if (pressStartMs == 0) pressStartMs = now;
  if ((uint32_t)(now - pressStartMs) >= BTN_LONGPRESS_MS) return true;

  return false;
}


// ================== Top bar rendering ==================
// NOTE: All top-bar separators are printed WITHOUT spaces.
//       Spaces are explicit characters at fixed positions.
#include <avr/pgmspace.h>

static void drawTopBar(PGM_P titleOrNull) {
  // Free mode: do NOT touch left area; just keep battery icon fresh
  if (titleOrNull == NULL) {
    drawBatteryTopBarIfNeeded();
    return;
  }

  // Title mode (including "" which means "clear bar but print no title")
  tft_fillRect(0, TOPBAR_Y, (int16_t)tft_width(), TOPBAR_H, C_LIGHTGREY);

  // PROGMEM-safe "is string non-empty?"
  char c0 = (char)pgm_read_byte(titleOrNull);
  if (c0 != 0) {
    tft_setTextSize(1);
    tft_setTextColor(C_BLACK, C_LIGHTGREY);

    const int16_t x0 = 2;                 // small left padding
    const int16_t y0 = TB_TEXT_Y;

    // Available pixels before the reserved battery area
    const int16_t maxPx = (int16_t)tft_width() - TOPBAR_BATT_W - x0;
    const int16_t maxChars = (maxPx > 0) ? (maxPx / FONT_W) : 0;

    tft_setCursor(x0, y0);

    // Print clipped (no heap, no String) -- read from PROGMEM safely
    for (int16_t i = 0; i < maxChars; i++) {
      char ch = (char)pgm_read_byte(titleOrNull + i);
      if (ch == 0) break;
      tft_print_char(ch);
    }
  }

  // Always draw battery on the right
  drawBatteryTopBarIfNeeded();
}

static void drawTopBarStatic() {
  // Background is drawn by drawTopBar("") on screen entry.
  tft_setTextSize(1);
  tft_setTextColor(C_BLACK, C_LIGHTGREY);

  tft_setCursor(X(COL_SWR_LABEL) + 2, TB_TEXT_Y);
  tft_print_P(PSTR("SWR="));

  tft_setCursor(X(COL_LBRACKET), TB_TEXT_Y);
  tft_print_P(PSTR("["));

  tft_setCursor(X(COL_DASH), TB_TEXT_Y);
  tft_print_P(PSTR(".."));

  tft_setCursor(X(COL_RBRACKET), TB_TEXT_Y);
  tft_print_P(PSTR("]"));

  tft_setCursor(X(COL_FREQ_LABEL), TB_TEXT_Y);
  tft_print_P(PSTR("F="));
}

static void padLeftToWidth(char* s, uint8_t width) {
  uint8_t len = (uint8_t)strlen(s);
  if (len >= width) return;
  uint8_t pad = (uint8_t)(width - len);
  memmove(s + pad, s, len + 1);     // shift right including '\0'
  memset(s, ' ', pad);             // leading spaces
}

static void drawTopBarValues(uint8_t flags = TB_UPD_ALL) {
  char buf[16];

  const bool force = (flags & TB_FORCE) != 0;

  // Keep style consistent (you already noticed you need this often)
  tft_setTextSize(1);

  // ---------- SWR current ----------
  if ((flags & TB_UPD_SWR_CURR) != 0) {
    if (force || swrCurrent_x100 != last_swrCurrent_x100) {
      last_swrCurrent_x100 = swrCurrent_x100;

      fmtSWRx100(buf, swrCurrent_x100);   // e.g. "1.00" (4 chars)
      // If fmtSWRx100 is ever shorter, force fixed width:
      padLeftToWidth(buf, 4);

      tft_setTextColor(C_BLACK, C_LIGHTGREY);
      tft_setCursor(X(COL_SWR_NOW) + 1, TB_TEXT_Y);
      tft_print_str(buf);                 // no fillRect needed if width fixed
    }
  }

  // ---------- SWR min/max ----------
  if ((flags & TB_UPD_SWR_RANGE) != 0) {
    if (force || swrMinCur_x100 != last_swrMinCur_x100) {
      last_swrMinCur_x100 = swrMinCur_x100;

      fmtSWRx100(buf, swrMinCur_x100);
      padLeftToWidth(buf, 4);

      tft_setTextColor(C_BLACK, C_LIGHTGREY);
      tft_setCursor(X(COL_SWR_MIN), TB_TEXT_Y);
      tft_print_str(buf);
    }

    if (force || swrMaxCur_x100 != last_swrMaxCur_x100) {
      last_swrMaxCur_x100 = swrMaxCur_x100;

      fmtSWRx100(buf, swrMaxCur_x100);
      padLeftToWidth(buf, 4);

      tft_setTextColor(C_BLACK, C_LIGHTGREY);
      tft_setCursor(X(COL_SWR_MAX), TB_TEXT_Y);
      tft_print_str(buf);
    }
  }

  // ---------- Frequency (most frequent update on encoder) ----------
  if ((flags & TB_UPD_FREQ) != 0) {
    if (force || cursorHz != last_cursorHz) {
      last_cursorHz = cursorHz;

      // You said: graph uses 4 decimals (incl. 60m), no leading 0
      fmtFreq(buf, cursorHz, 4);          // e.g. "9.9990" or "10.0000"
      padLeftToWidth(buf, 7);             // always occupy 7 columns

      tft_setTextColor(C_BLACK, C_LIGHTGREY);
      tft_setCursor(X(COL_FREQ_VAL), TB_TEXT_Y);
      tft_print_str(buf);                 // no fillRect => much less flicker
    }
  }

  // ---------- Band label / USER range ----------
  // (rarely changes; safe to clear region when it does)
  if ((flags & TB_UPD_BAND) != 0) {
    bool bandChanged = force || (currentBand != last_band);
    bool userChanged = false;

    if (currentBand == BAND_USER) {
      // USER range changes only in menu; still safe to detect
      userChanged = force || (userBandLoHz != last_userLoHz) || (userBandHiHz != last_userHiHz);
    }

    if (bandChanged || userChanged) {
      last_band = currentBand;
      last_userLoHz = userBandLoHz;
      last_userHiHz = userBandHiHz;

      const int16_t x0   = X(COL_BAND_VAL);
      const int16_t xMax = (int16_t)tft_width() - TOPBAR_BATT_W;

      if (xMax > x0) {
        tft_fillRect(x0, TB_TEXT_Y, (int16_t)(xMax - x0), FONT_H, C_LIGHTGREY);
      }

      tft_setTextColor(C_BLACK, C_LIGHTGREY);
      tft_setCursor(x0, TB_TEXT_Y);

      if (currentBand != BAND_USER) {
        // fixed band name
        tft_print_str(bandName[currentBand]);
      } else {
        // USER: print "xx.xxx-yy.yyy" (NO brackets)
        // Build from Hz to 3 decimals.
        // lo/hi in Hz => MHz.xxx
        uint32_t lo_khz = (userBandLoHz + 500UL) / 1000UL;
        uint32_t hi_khz = (userBandHiHz + 500UL) / 1000UL;

        uint16_t lo_mhz = (uint16_t)(lo_khz / 1000UL);
        uint16_t lo_frac = (uint16_t)(lo_khz % 1000UL);
        uint16_t hi_mhz = (uint16_t)(hi_khz / 1000UL);
        uint16_t hi_frac = (uint16_t)(hi_khz % 1000UL);

        // reuse buf
        // format: "<mhz>.<fff>-<mhz>.<fff>"
        char* p = buf;

        if (lo_mhz >= 10) *p++ = (char)('0' + (lo_mhz / 10));
        *p++ = (char)('0' + (lo_mhz % 10));
        *p++ = '.';
        *p++ = (char)('0' + (lo_frac / 100));
        *p++ = (char)('0' + (lo_frac / 10) % 10);
        *p++ = (char)('0' + (lo_frac % 10));
        *p++ = '-';
        if (hi_mhz >= 10) *p++ = (char)('0' + (hi_mhz / 10));
        *p++ = (char)('0' + (hi_mhz % 10));
        *p++ = '.';
        *p++ = (char)('0' + (hi_frac / 100));
        *p++ = (char)('0' + (hi_frac / 10) % 10);
        *p++ = (char)('0' + (hi_frac % 10));
        *p = 0;

        tft_print_str(buf);
      }
    }
  }
}


// ================== GRAPH SCREEN (NEW) ==================

static inline uint16_t swrMaxForSel_x100() {
  // swrRangeSel values: 0="1..10", 1="1..5", 2="1..2.5"
  if (swrRangeSel == 1) return 500;
  if (swrRangeSel == 2) return 250;
  return 1000;
}

static inline uint16_t swrTickStepForSel_x100() {
  // 1..10: 0.5 ; 1..5: 0.25 ; 1..2.5: 0.10
  if (swrRangeSel == 1) return 20;
  if (swrRangeSel == 2) return 10;
  return 50;
}

static void getBandEdgesHz(BandId b, uint32_t& lo, uint32_t& hi) {
  if (b == BAND_USER) {
    lo = userBandLoHz;
    hi = userBandHiHz;
    return;
  }
  lo = bandRange[b].fLo;
  hi = bandRange[b].fHi;
}

static uint32_t cursorHzFromX(uint16_t cx, uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;

  const uint32_t denom = (uint32_t)(GRAPH_W - 1);
  if (denom == 0) return lo;

  if (cx >= denom) return hi;

  const uint32_t span = hi - lo;

  // 64-bit to avoid overflow when span is large (e.g. 1..60 MHz with GRAPH_W=300)
  const uint64_t num = (uint64_t)cx * (uint64_t)span + (uint64_t)denom / 2ULL; // rounding
  const uint32_t off = (uint32_t)(num / (uint64_t)denom);

  uint32_t hz = lo + off;
  if (hz > hi) hz = hi; // safety clamp
  return hz;
}

static void ensureCursorValid(BandId b) {
  if (cursorX_byBand[b] == CURSOR_INVALID || cursorX_byBand[b] >= (uint16_t)GRAPH_W) {
    cursorX_byBand[b] = (uint16_t)(GRAPH_W / 2);
  }
}

static void syncCursorDerived() {
  ensureCursorValid(currentBand);

  uint32_t lo, hi;
  getBandEdgesHz(currentBand, lo, hi);

  uint16_t cx = cursorX_byBand[currentBand];
  cursorHz = cursorHzFromX(cx, lo, hi);

  if (graphHasData) {
    uint16_t v = pack10_read_x100(cx);
    swrCurrent_x100 = (v != 0) ? v : 100;   // invalid sample => show 1.00
  } else {
    swrCurrent_x100 = 100; // 1.00 until we have data
  }
}

static inline bool graphPyFromIndex(uint16_t xi, int16_t& py, bool& overflow) {
  overflow = false;

  if (!graphHasData) return false;
  if (xi >= (uint16_t)GRAPH_W) return false;

  const uint16_t s = pack10_read_x100(xi);
  if (s == 0) return false;               // invalid / not measured (sentinel)

  const uint16_t SWR_MAX_X100 = swrMaxForSel_x100();
  const uint16_t SPAN_X100 = (SWR_MAX_X100 > 100) ? (SWR_MAX_X100 - 100) : 1;

  overflow = (s > SWR_MAX_X100);

  // Clamp to active UI SWR range for plotting (SSOT 18.12.1) :contentReference[oaicite:6]{index=6}
  uint16_t vc = s;
  if (vc < 100) vc = 100;
  if (vc > SWR_MAX_X100) vc = SWR_MAX_X100;

  // Integer version of the SSOT mapping (equivalent to round(t*(GRAPH_H-1))) :contentReference[oaicite:7]{index=7}
  const int16_t GH = (gGH > 0) ? gGH : GRAPH_H;

  uint32_t num = (uint32_t)(vc - 100) * (uint32_t)(GH - 1);
  uint16_t dy  = (uint16_t)((num + (SPAN_X100 / 2)) / SPAN_X100);

  py = (int16_t)(gGY1 - (int16_t)dy);
  if (py < gGY0) py = gGY0;
  if (py > gGY1) py = gGY1;
  return true;
}


static void drawCursorCrossAtIndex(uint16_t xi, uint16_t color) {
  if (!gGraphGeomValid) return;

  const int16_t cxPix = (int16_t)(gGX0 + (int16_t)xi);

  // vertical dashed always
  for (int16_t y = gGY0; y <= (gY_AXIS - 1); y++) {
    if (((y - gGY0) % 6) < 4) tft_drawPixel(cxPix, y, color);
  }

  // horizontal only if we have a valid sample at xi
  int16_t cyPix; bool ov;
  if (!graphPyFromIndex(xi, cyPix, ov)) return;

  if (cyPix <= gGY0) return;

  const int16_t x0 = (int16_t)(gX_AXIS + 1);
  const int16_t x1 = gGX1;
  for (int16_t x = x0; x <= x1; x++) {
    if (((x - x0) % 3) == 0) tft_drawPixel(x, cyPix, color);
  }
}

static void drawCursorVerticalAtIndex(uint16_t xi, uint16_t color) {
  if (!gGraphGeomValid) return;

  const int16_t cxPix = (int16_t)(gGX0 + (int16_t)xi);

  for (int16_t y = gGY0; y <= (gY_AXIS - 1); y++) {
    if (((y - gGY0) % 6) < 4) tft_drawPixel(cxPix, y, color);
  }
}

static void redrawGraphCurveAll() {
  if (!gGraphGeomValid) return;
  if (!graphHasData) return;

  const uint16_t limit = (gGW > 0) ? (uint16_t)gGW : (uint16_t)GRAPH_W;

  bool    havePrev = false;
  int16_t pxPrev = 0, pyPrev = 0;
  bool    ovPrev = false;

  for (uint16_t xi = 0; xi < limit; xi++) {
    int16_t py; bool ov;
    if (!graphPyFromIndex(xi, py, ov)) {
      havePrev = false;
      continue;
    }

    const int16_t px = (int16_t)(gGX0 + (int16_t)xi);

    if (havePrev) {
      const uint16_t c = (ovPrev || ov) ? C_RED : C_YELLOW;
      tft_drawLine(pxPrev, pyPrev, px, py, c);
    } else {
      // first point of a segment: at least mark it
      tft_drawPixel(px, py, ov ? C_RED : C_YELLOW);
    }

    // Ensure overflow sample is visibly red even if the segment color was yellow
    if (ov) tft_drawPixel(px, py, C_RED);

    pxPrev = px;
    pyPrev = py;
    ovPrev = ov;
    havePrev = true;
  }
}

static void moveGraphCursor(uint16_t oldXi, uint16_t newXi) {
  if (!gGraphGeomValid) return;
  if (oldXi == newXi) return;

    // erase old cursor cross
  drawCursorCrossAtIndex(oldXi, C_BLACK);

  // restore graph (horizontal cursor can erase curve pixels across the width)
  redrawGraphCurveAll();

  // draw new cursor cross
  drawCursorCrossAtIndex(newXi, C_WHITE);

}

static void drawGraphScreen() {
  // Keep top bar values coherent with cursor position
  syncCursorDerived();

  // --- top bar ---
  drawTopBar(PSTR(""));
  drawTopBarStatic();
  // force first draw of dynamic values
  drawTopBarValues(TB_UPD_ALL | TB_FORCE);


  // --- geometry (SSOT rev13/18) ---
  const int16_t W = (int16_t)tft_width();
  const int16_t H = (int16_t)tft_height();

  const int16_t WORK_Y0 = TOPBAR_Y + TOPBAR_H + TOPBAR_GAP;
  const int16_t WORK_H  = H - WORK_Y0;

  // clear work area
  tft_fillRect(0, WORK_Y0, W, WORK_H, C_BLACK);

  const int16_t X_AXIS = 1 + FONT_W + 1 + NOTCH_W;
  const int16_t Y_AXIS = (H - 1) - (1 + FONT_H + 1 + NOTCH_H);

  const int16_t PLOT_X0 = X_AXIS + 1;
  const int16_t PLOT_Y0 = WORK_Y0;
  const int16_t PLOT_X1 = W - 1;
  const int16_t PLOT_Y1 = Y_AXIS - 1;

  const int16_t PLOT_W = PLOT_X1 - PLOT_X0 + 1;
  const int16_t PLOT_H = PLOT_Y1 - PLOT_Y0 + 1;

  // Clamp inner graph size if ever needed
  const int16_t GW = (PLOT_W < GRAPH_W) ? PLOT_W : GRAPH_W;
  const int16_t GH = (PLOT_H < GRAPH_H) ? PLOT_H : GRAPH_H;

  const int16_t PAD_X = (PLOT_W - GW) / 2;
  const int16_t PAD_Y = (PLOT_H - GH) / 2;

  const int16_t GX0 = PLOT_X0 + PAD_X;
  const int16_t GY0 = PLOT_Y0 + PAD_Y;
  const int16_t GX1 = GX0 + GW - 1;
  const int16_t GY1 = GY0 + GH - 1;

  gGX0 = GX0;
  gGY0 = GY0;
  gGY1 = GY1;
  gX_AXIS = X_AXIS;
  gY_AXIS = Y_AXIS;
  gGX1 = GX1;
  gGraphGeomValid = true;

  // --- axes ---
  tft_drawFastVLine(X_AXIS, PLOT_Y0, (Y_AXIS - PLOT_Y0 + 1), C_WHITE);
  tft_drawFastHLine(X_AXIS, Y_AXIS, (W - X_AXIS), C_WHITE);

  // --- SWR ticks + labels (left axis) ---
  tft_setTextSize(1);
  tft_setTextColor(C_WHITE, C_BLACK);

  const uint16_t SWR_MAX_X100 = swrMaxForSel_x100();
  const uint16_t TICK_STEP_X100 = swrTickStepForSel_x100();
  const uint16_t SPAN_X100 = (SWR_MAX_X100 > 100) ? (SWR_MAX_X100 - 100) : 1;

  for (uint16_t s = 100; s <= SWR_MAX_X100; s = (uint16_t)(s + TICK_STEP_X100)) {
    uint32_t num = (uint32_t)(s - 100) * (uint32_t)(GH - 1);
    uint16_t dy  = (uint16_t)((num + (SPAN_X100 / 2)) / SPAN_X100);
    int16_t y = (int16_t)(GY1 - (int16_t)dy);

    tft_drawFastHLine((int16_t)(X_AXIS - NOTCH_W), y, NOTCH_W, C_WHITE);

    // label selection (do not print the highest)
    bool printLabel = false;
    uint8_t labelDigit = 0;

    static const uint8_t top[] = { 9, 4, 2 };   // swrRangeSel 0,1,2

    if (s % 100 == 0 && s >= 100 && s <= top[swrRangeSel] * 100) {
        printLabel = true;
        labelDigit = s / 100;
    }

    if (printLabel) {
      int16_t ly = (int16_t)(y - (FONT_H / 2));
      if (ly < PLOT_Y0) ly = PLOT_Y0;
      if (ly > (Y_AXIS - FONT_H)) ly = (int16_t)(Y_AXIS - FONT_H);

      tft_setCursor(1, ly);
      tft_print_int(labelDigit);
    }

    if (s == SWR_MAX_X100) break;
  }

  // --- Frequency ticks + labels (bottom axis) ---
  uint32_t fLo, fHi;
  getBandEdgesHz(currentBand, fLo, fHi);

  const int16_t LABEL_Y = (H - FONT_H);  // bottom label baseline

  for (uint8_t i = 0; i < 5; i++) {
    uint32_t num = (uint32_t)i * (uint32_t)(GW - 1);
    uint16_t dx = (uint16_t)((num + 2) / 4);   // 4 segments => 5 ticks
    int16_t x = (int16_t)(GX0 + dx);

    tft_drawFastVLine(x, (int16_t)(Y_AXIS + 1), NOTCH_H, C_WHITE);

    if (i < 4) {
      uint32_t hz = fLo;
      if (fHi > fLo) {
        uint32_t span = fHi - fLo;
        hz = fLo + (uint32_t)(((uint64_t)i * span + 2) / 4);
      }

      char fb[8];
      fmtFreq(fb, hz,2);

      uint8_t n = 0; while (fb[n]) n++;
      int16_t textW = (int16_t)(n * FONT_W);

      int16_t lx;
      if (i == 0) {
        lx = PLOT_X0;  // left aligned
      } else {
        lx = (int16_t)(x - textW / 2);
        if (lx < PLOT_X0) lx = PLOT_X0;
      }
      if (lx > (W - textW)) lx = (int16_t)(W - textW);

      tft_setCursor(lx, LABEL_Y);
      tft_print_str(fb);
    }
  }

  // --- curve ---
   redrawGraphCurveAll();

  // --- cursor cross (SSOT 18.11) ---
  if (!sweepInProgress) {
    ensureCursorValid(currentBand);
    drawCursorCrossAtIndex(cursorX_byBand[currentBand], C_WHITE);
  }
}

static void runMeasurementStub() {
  // ------------- setup -------------
  if (!isCalibratedNow()) {
    showCalibrationNeededPopup();
    return;
  }

  sweepInProgress = true;

  uint32_t loHz, hiHz;
  getBandEdgesHz(currentBand, loHz, hiHz);

  // Clear buffer first
  pack10_clear();

  // No data yet
  graphHasData = false;
  swrMinCur_x100 = 100;
  swrMaxCur_x100 = 100;

  // Keep axes/labels intact; only clear the inner plot area to reduce flicker.
  if (!gGraphGeomValid) {
    drawGraphScreen();
    if (!gGraphGeomValid) {
      sweepInProgress = false;
      return;
    }
  } else {
    clearPlotAreaOnly();
  }

  const uint16_t cursorXi = cursorX_byBand[currentBand];
  drawCursorVerticalAtIndex(cursorXi, C_WHITE);

  // Progress line bookkeeping (draw only new segment)
  uint16_t progressDrawn = 0;

  // Incremental curve bookkeeping
  bool    havePrev = false;
  int16_t pxPrev = 0, pyPrev = 0;
  bool    ovPrev = false;

  // Min/max tracking (use quantized stored values so UI matches stored data)
  uint16_t minv = 0xFFFF;
  uint16_t maxv = 0;
  bool any = false;

  uint32_t lpStartMs = 0;

  // ------------- sweep loop -------------
  for (uint16_t xi = 0; xi < (uint16_t)GRAPH_W; xi++) {

    // Allow entering menu even during sweep (SSOT rev24)
    if (sweepAbortToMenuRequested(lpStartMs)) {
      sweepInProgress = false;
      aaSetFreq(0, 0);               // ensure outputs off
      invalidateGraphData();         // old curve is now invalid
      autoScheduleFromNowFullInterval(); // schedule after full interval
      enterScreen(SCR_MAIN_MENU);
      return;
    }

    // Frequency for this X (uses your existing mapping)
    const uint32_t fHz = cursorHzFromX(xi, loHz, hiHz);

    // Actual SWR measurement
    const float swr = aaReadSWR(fHz);

    uint16_t v = 0; // 0 means invalid/unmeasured
    if (swr > 0.0f) {
      uint32_t x100 = (uint32_t)(swr * 100.0f + 0.5f);
      if (x100 < 100UL) x100 = 100UL;
      if (x100 > (uint32_t)PACK10_MAX_X100) x100 = (uint32_t)PACK10_MAX_X100;
      v = (uint16_t)x100;
    }

    // Store packed (quantized)
    pack10_write_x100(xi, v);
    const uint16_t q = pack10_read_x100(xi);   // 0 or quantized x100

    // Update progress line on the X-axis (green), draw only the delta
    const uint16_t want = (uint16_t)(xi + 1);
    if (want > progressDrawn) {
      const int16_t x0 = (int16_t)(gGX0 + (int16_t)progressDrawn);
      const int16_t w  = (int16_t)(want - progressDrawn);
      tft_drawFastHLine(x0, gY_AXIS, w, C_GREEN);
      progressDrawn = want;
    }

    // Ignore invalid samples for curve and min/max
    if (q == 0) {
      havePrev = false;
      continue;
    }

    // We now have real data
    if (!graphHasData) graphHasData = true;

    any = true;
    if (q < minv) minv = q;
    if (q > maxv) maxv = q;

    // Compute pixel Y for this point (and overflow flag) using your existing mapper
    int16_t py; bool ov;
    if (!graphPyFromIndex(xi, py, ov)) {
      havePrev = false;
      continue;
    }

    const int16_t px = (int16_t)(gGX0 + (int16_t)xi);

    // Draw incremental curve segment
    if (havePrev) {
      const uint16_t c = (ovPrev || ov) ? C_RED : C_YELLOW;
      tft_drawLine(pxPrev, pyPrev, px, py, c);
    } else {
      tft_drawPixel(px, py, ov ? C_RED : C_YELLOW);
    }

    // Ensure overflow sample is visibly red (same behavior as full redraw)
    if (ov) tft_drawPixel(px, py, C_RED);

    if (xi == cursorXi) {
      drawCursorVerticalAtIndex(cursorXi, C_WHITE);
    }

    pxPrev = px;
    pyPrev = py;
    ovPrev = ov;
    havePrev = true;
  }

  // ------------- finalize -------------
  graphHasData   = any;
  swrMinCur_x100 = any ? minv : 100;
  swrMaxCur_x100 = any ? maxv : 100;

  // Refresh top bar SWR at the current cursor after sweep completes.
  syncCursorDerived();
  drawTopBarValues(TB_UPD_SWR_RANGE | TB_UPD_SWR_CURR | TB_UPD_FREQ);

  // IMPORTANT: outputs should not remain active after sweep
  aaSetFreq(0, 0);

  // Turn progress line back to white (restore X-axis)
  // (Axis originally drawn from gX_AXIS to screen right)
  tft_drawFastHLine(gX_AXIS, gY_AXIS, (int16_t)(tft_width() - gX_AXIS), C_WHITE);

  sweepInProgress = false;

  // Draw cursor cross now that sweep is finished
  ensureCursorValid(currentBand);
  drawCursorCrossAtIndex(cursorX_byBand[currentBand], C_WHITE);
}

// ============================================================
// Battery icon + percent (UNCHANGED)
// ============================================================
static uint8_t batteryPercentFromVolts(float v) {
  if (v <= BATT_CAL_V_MIN) return 0;
  if (v >= BATT_FULL_V) return 100;
  float p = (v - BATT_CAL_V_MIN) / (BATT_FULL_V - BATT_CAL_V_MIN);
  uint8_t pct = (uint8_t)(p * 100.0f + 0.5f);
  if (pct > 100) pct = 100;
  return pct;
}

static void drawBatteryIcon(int16_t x, int16_t y, uint8_t pct, bool charging) {
  tft_fillRect(x, y, BATT_ICON_W, BATT_ICON_H, C_LIGHTGREY);

  int16_t bx = x + 1;
  int16_t by = y + 1;

  tft_drawRect(bx, by, BATT_BODY_W, BATT_BODY_H, C_BLACK);
  tft_drawRect(
    bx + BATT_BODY_W,
    by + (BATT_BODY_H - BATT_NUB_H) / 2,
    BATT_NUB_W,
    BATT_NUB_H,
    C_BLACK
  );

  const int16_t innerX = bx + 2;
  const int16_t innerY = by + 2;
  const int16_t innerW = BATT_BODY_W - 4;
  const int16_t innerH = BATT_BODY_H - 4;

  tft_fillRect(innerX, innerY, innerW, innerH, C_LIGHTGREY);

  int16_t fillW = (int16_t)((uint32_t)innerW * pct / 100);
  if (fillW < 0) fillW = 0;
  if (fillW > innerW) fillW = innerW;

  uint16_t fillColor = C_GREEN;
  if (pct < 15) fillColor = C_RED;
  else if (pct < 40) fillColor = C_YELLOW;

  if (fillW > 0) {
    tft_fillRect(innerX, innerY, fillW, innerH, fillColor);
  }

  if (charging) {
    // simple lightning bolt inside the battery (no fonts required)

    // Bolt vertical extent in your drawing:
    // start at cy, ends at (cy + 8) => height = 9 px (0..8)
    const int16_t BOLT_H = 9;

    int16_t cx = innerX + innerW / 2;

    // Center the bolt vertically inside innerY..innerY+innerH-1
    // (Top of bolt placed so its 9px height is centered)
    int16_t cy = innerY + (innerH - BOLT_H) / 2;

    // Optional: fine-tune by 1px if the diagonals look visually high/low
    // cy += 0;   // +1 moves down, -1 moves up

    tft_drawLine(cx,     cy,     cx - 3, cy + 4, C_BLACK);
    tft_drawLine(cx - 3, cy + 4, cx + 2, cy + 4, C_BLACK);
    tft_drawLine(cx + 2, cy + 4, cx - 2, cy + 8, C_BLACK);
  }
}

static int16_t lastBattMvShown = -1;
static uint8_t lastBattPctShown = 255;
static uint32_t battUiDueMs = 0;

// Draw battery icon into top bar area (right side)
static void drawBatteryTopBarIfNeeded() {
  const uint32_t now = millis();
  if ((int32_t)(now - battUiDueMs) < 0) return;
  battUiDueMs = now + 500;

  if (!battInit) return;

  const float v = battVoltsEma;
  const int16_t mv = (int16_t)(v * 1000.0f + 0.5f);
  const uint8_t pct = batteryPercentFromVolts(v);

  // --- charging detect with hysteresis ---
  if (!battChargingUi) {
    if (v > BATT_CHARGE_ON_V) battChargingUi = true;
  } else {
    if (v < (BATT_CHARGE_ON_V - BATT_CHARGE_HYS)) battChargingUi = false;
  }

  const bool charging = battChargingUi;

  // NOTE: include charging in cache check, otherwise icon might not update
  if (mv == lastBattMvShown && pct == lastBattPctShown && charging == lastBattChargingShown) return;

  lastBattMvShown = mv;
  lastBattPctShown = pct;
  lastBattChargingShown = charging;

  lastBattMvShown = mv;
  lastBattPctShown = pct;

  // Clear ONLY the reserved battery area (invariant)
  tft_fillRect((int16_t)tft_width() - TOPBAR_BATT_W, TOPBAR_Y, TOPBAR_BATT_W, TOPBAR_H, C_LIGHTGREY);

  const int16_t x = (int16_t)tft_width() - BATT_ICON_W - BATT_MARGIN_RIGHT;
  const int16_t y = TOPBAR_Y + (TOPBAR_H - BATT_ICON_H) / 2;

  drawBatteryIcon(x, y, pct, charging);
}


// ============================================================
// Soft-latch
// ============================================================
static void drawShutdownOverlayOnce() {
  if (shutdownMsgShown) return;
  shutdownMsgShown = true;

  const int16_t bw = 200;
  const int16_t bh = 60;
  const int16_t x = (int16_t)(tft_width()  - bw) / 2;
  const int16_t y = (int16_t)(tft_height() - bh) / 2;

  tft_fillRect(x, y, bw, bh, C_BLACK);
  tft_drawRect(x, y, bw, bh, C_WHITE);

  tft_setTextSize(2);
  tft_setTextColor(C_WHITE, C_BLACK);
  tft_setCursor(x + 20, y + 22);
  tft_print_P(PSTR("Shutting Down"));
}

static void cutPowerNow() {
  digitalWrite(PIN_PWR_HOLD, LOW);
  pinMode(PIN_PWR_HOLD, INPUT); // Hi-Z
}

static void initSoftLatchPins() {
  pinMode(PIN_PWR_HOLD, OUTPUT);
  digitalWrite(PIN_PWR_HOLD, HIGH);
  pinMode(PIN_PWR_BTN_SENSE, INPUT);
  shutdownArmed = false;
  shutdownMsgShown = false;
  k0LowSinceMs = 0;
}

static void serviceSoftLatch() {
  const uint32_t now = millis();
  const bool pressed = (digitalRead(PIN_PWR_BTN_SENSE) == LOW);

  if (!shutdownArmed) {
    if (pressed) {
      if (k0LowSinceMs == 0) k0LowSinceMs = now;
      if ((uint32_t)(now - k0LowSinceMs) >= SHUTDOWN_HOLD_MS) {
        shutdownArmed = true;
        drawShutdownOverlayOnce();
#if DEBUG
        DBG_PRINTLN("[PWR] shutdown armed");
#endif
      }
    } else {
      k0LowSinceMs = 0;
    }
  } else {
    if (!pressed) {
#if DEBUG
      DBG_PRINTLN("[PWR] cutting power");
#endif
      cutPowerNow();
    }
  }
}

// ============================================================
// Battery sense
// ============================================================
static void initBatterySense() {
  analogReference(INTERNAL);
  pinMode(PIN_BATT_SENSE, INPUT);
  battSampleDueMs = 0;
  battInit = false;
  battVoltsEma = 0.0f;
  battVoltsUncal = 0.0f;
}

static void serviceBatterySense() {
  const uint32_t now = millis();
  if ((int32_t)(now - battSampleDueMs) < 0) return;
  battSampleDueMs = now + 200;

  int raw = analogRead(PIN_BATT_SENSE);
  float vadc = (raw * ADC_VREF_INTERNAL) / 1023.0f;

  battRawLast = raw;
  battVadcLast = vadc;

  float vbatt_uncal = vadc / BATT_DIV_RATIO;
  battVoltsUncal = vbatt_uncal;

  float vbatt = vbatt_uncal * battCalScale;

  const float alpha = 0.10f;
  if (!battInit) {
    battVoltsEma = vbatt;
    battInit = true;
  } else {
    battVoltsEma += alpha * (vbatt - battVoltsEma);
  }

#if BATT_DEBUG
  static uint32_t nextLog = 0;
  if ((int32_t)(now - nextLog) >= 0) {
    nextLog = now + 1000;
    DBG_PRINT("[BATT] raw="); DBG_PRINT(raw);
    DBG_PRINT(" uncal="); DBG_PRINTF(battVoltsUncal, 3);
    DBG_PRINT(" K="); DBG_PRINTF(battCalScale, 5);
    DBG_PRINT(" cal="); DBG_PRINTLNF(getBatteryVolts(), 3);
  }
#endif
}

static float getBatteryVolts() { return battVoltsEma; }


// ============================================================
// EEPROM
// ============================================================
static bool eepromIsValidStruct(const EepromSettings& s) {
  if (s.magic != EEPROM_MAGIC) return false;
  if (s.eeversion != EEPROM_VER) return false;

  if (s.batt_scale < 0.5f || s.batt_scale > 1.5f) return false;
  if (s.current_band >= (uint8_t)BAND_COUNT) return false;
  if (s.swr_range_sel >= SWR_RANGE_N) return false;
  if (s.auto_rescan_sel >= AR_N) return false;

  // USER band bounds sanity (allow 1–60 MHz range for future expansion)
  if (s.user_lo_hz < 1000000UL) return false;
  if (s.user_hi_hz > 60000000UL) return false;
  if (s.user_hi_hz <= s.user_lo_hz) return false;
  
  if (s.quartz_hz < XTAL_MIN || s.quartz_hz > XTAL_MAX) return false;
  
  // Cursor indices: allow CURSOR_INVALID sentinel; otherwise must be within inner graph width
  for (uint8_t i = 0; i < (uint8_t)BAND_COUNT; i++) {
    uint16_t cx = s.cursor_x[i];
    if (cx != CURSOR_INVALID && cx >= (uint16_t)GRAPH_W) return false;
  }

  return true;
}

static bool eepromIsValid() {
  EepromSettings s;
  EEPROM.get(EEPROM_ADDR, s);
  return eepromIsValidStruct(s);
}

static void eepromLoad() {
  EepromSettings s;
  EEPROM.get(EEPROM_ADDR, s);

  if (!eepromIsValidStruct(s)) {
    // defaults
    battCalScale = 1.0f;
    currentBand  = BAND_40M;

    userBandLoHz = 1000000UL;
    userBandHiHz = 60000000UL;
    quartzHz = QTZ_DEFAULT_HZ;

    // SSOT rev13: cursor defaults to center for all bands
    for (uint8_t i = 0; i < (uint8_t)BAND_COUNT; i++) {
      cursorX_byBand[i] = (uint16_t)(GRAPH_W / 2);
    }

    // Defaults for menu settings
    swrRangeSel = 0;   // "1..10" (SSOT default)
    autoRescanSel = AR_NONE; // SSOT default

    eepromSave(); // initialize EEPROM with defaults (one-time after version bump)
    return;
  }

  battCalScale = s.batt_scale;
  currentBand  = (BandId)s.current_band;

  userBandLoHz = s.user_lo_hz;
  userBandHiHz = s.user_hi_hz;
  quartzHz = s.quartz_hz;
  swrRangeSel = (s.swr_range_sel < SWR_RANGE_N) ? s.swr_range_sel : 0;
  autoRescanSel = (s.auto_rescan_sel < AR_N) ? (AutoRescanSel)s.auto_rescan_sel : AR_NONE;

  // RAM-only settings default on every boot (per SSOT)
  ubStepSel   = 1;  // 100 kHz
  xtalStepSel = 0;  // 1 Hz

  // Cursor per band (allow sentinel)
  for (uint8_t i = 0; i < (uint8_t)BAND_COUNT; i++) {
    uint16_t cx = s.cursor_x[i];
    cursorX_byBand[i] = (cx == CURSOR_INVALID || cx >= (uint16_t)GRAPH_W) ? (uint16_t)(GRAPH_W / 2) : cx;
  }

}

static void eepromSave() {
  EepromSettings s;

  s.magic      = EEPROM_MAGIC;
  s.eeversion  = EEPROM_VER;
  s.fw_major   = FW_VER_MAJOR;
  s.fw_minor   = FW_VER_MINOR;

  s.current_band = (uint8_t)currentBand;
  s.swr_range_sel  = swrRangeSel;
  s.auto_rescan_sel = autoRescanSel;

  // Cursor per band
  for (uint8_t i = 0; i < (uint8_t)BAND_COUNT; i++) {
    s.cursor_x[i] = cursorX_byBand[i];
  }

  s.batt_scale = battCalScale;

  s.user_lo_hz = userBandLoHz;
  s.user_hi_hz = userBandHiHz;
  s.quartz_hz = quartzHz;

  EEPROM.put(EEPROM_ADDR, s);
}


static void resetToDefaultsNow() {
  // Defaults are already defined by your eepromLoad() invalid-EEPROM path (~1110-1114)
  battCalScale = 1.0f;
  currentBand  = BAND_40M;

  userBandLoHz = 1000000UL;
  userBandHiHz = 60000000UL;

  // SSOT rev13: cursor defaults to center for all bands
  for (uint8_t i = 0; i < (uint8_t)BAND_COUNT; i++) {
    cursorX_byBand[i] = (uint16_t)(GRAPH_W / 2);
  }

  // RAM-only Settings (SSOT: Freq Display RAM-only and defaults to Linear after reset)
  swrRangeSel  = 0; // "1..10"

  autoRescanSel = AR_NONE;
  autoNextDueMs = 0;
  firstGraphEntryAfterBoot = true;

  // SSOT 17.6: Step is RAM-only and defaults to 100 kHz after reset
  ubStepSel = 1;
  xtalStepSel = 0;  // 1 Hz (RAM-only)

  // Clear any editing state so UI can't get stuck in edit mode
  menuEditMode     = false;
  bandDirty        = false;
  
  // Invalidate ZeroLevel calibration block (forces re-calibration)
  zeroLevel_factory_reset();

  // Persist defaults as a VALID EEPROM struct
  eepromSave();
}

// ============================================================
// System Info live update
// ============================================================
static void drawSystemInfoStatic() {
  tft_fillScreen(C_BLACK);
  drawTopBar(PSTR("Menu->System Info"));

  tft_setTextSize(1);
  tft_setTextColor(C_WHITE, C_BLACK);

  const int16_t x0 = 10;
  int16_t y = TOPBAR_H + 8;     // below top bar
  const int16_t dy = 12;

  // ---- SSOT header block ----
  tft_setCursor(x0, y); tft_print_P(PSTR("Simple Antenna Analyser")); y += dy;
  // empty line
  y += dy;
  tft_setCursor(x0, y); tft_print_P(PSTR("Design by W6EEN")); y += dy;
  tft_setCursor(x0, y); tft_print_P(PSTR("Based on the Pico-SWR from UR5FFR")); y += dy;

  // empty line
  y += dy;

  // Built on <date/time> (compile-time macros)
  tft_setCursor(x0, y);
  tft_print_P(PSTR("Built on "));
  tft_print_str(__DATE__);
  tft_print_char(' ');
  tft_print_str(__TIME__);
  y += (dy + 8);

  // ---- Fields (labels + value column) ----
  // Value column X chosen to keep labels readable and leave room for values.
  sysInfoValX = 140;
  sysInfoValW = (int16_t)tft_width() - sysInfoValX - 10;
  if (sysInfoValW < 20) sysInfoValW = 20;  // safety

  // Firmware Version
  tft_setCursor(x0, y); tft_print_P(PSTR("Firmware Version:"));
  tft_setCursor(sysInfoValX, y);
  tft_print_int(FW_VER_MAJOR);
  tft_print_char('.');
  if (FW_VER_MINOR < 10) tft_print_char('0');
  tft_print_int(FW_VER_MINOR);
  y += 14;

  // EEPROM Status
  tft_setCursor(x0, y); tft_print_P(PSTR("EEPROM Status:"));
  tft_setCursor(sysInfoValX, y);
  tft_print_P(eepromIsValid() ? PSTR("VALID") : PSTR("DEFAULTS"));
  y += 14;

  // Calibrated (ZeroLevel table present + valid)
  tft_setCursor(x0, y); tft_print_P(PSTR("Calibrated:"));
  tft_setCursor(sysInfoValX, y);
  {
    bool zlValid = false;
    uint8_t zlCount = 0;
    zeroLevel_get_state(&zlValid, &zlCount);
    tft_print_P(zlValid ? PSTR("yes") : PSTR("no"));
  }
  y += 14;

  // Battery Voltage (dynamic)
  tft_setCursor(x0, y); tft_print_P(PSTR("Battery Voltage:"));
  sysInfoBattVy = y;
  y += 14;

  // Battery % (dynamic)
  tft_setCursor(x0, y); tft_print_P(PSTR("Battery %:"));
  sysInfoBattPctY = y;
  y += 14;

  // Battery voltage adjustment coefficient
 // Battery K value
  tft_setCursor(x0, y); tft_print_P(PSTR("Battery Adj.:"));
  tft_setCursor(sysInfoValX, y);
  {
    char kbuf[16];
    fmtScale5dp(kbuf, battCalScale);
    tft_print_str(kbuf);
  }

  y += 14;

  // Quartz Frequency (real measured Si5351 quartz frequency)
  tft_setCursor(x0, y); tft_print_P(PSTR("Quartz Frequency:"));
  tft_setCursor(sysInfoValX, y);
  {
    char qbuf[12];
    ultoa(quartzHz, qbuf, 10);
    tft_print_str(qbuf);
  }

  tft_print_P(PSTR(" Hz"));
  y += 14;

  drawHelpLine(PSTR("Click: return to the menu"));

  // Reset live-update state and force first render
  lastSysBattMv = -1;
  lastSysBattPct = 255;
  sysInfoDueMs = 0;

  updateSystemInfoBatteryBox(true);
}

static void updateSystemInfoBatteryBox(bool force) {
  if (screen != SCR_SYSTEM_INFO) return;

  // Prepare text style
  tft_setTextSize(1);
  tft_setTextColor(C_WHITE, C_BLACK);

  const int16_t h = FONT_H + 2; // clear box height
 
  
  // If battery subsystem not ready
  if (!battInit) {
    if (!force) return;

    // Voltage N/A
    tft_fillRect(sysInfoValX, sysInfoBattVy, sysInfoValW, h, C_BLACK);
    tft_setCursor(sysInfoValX, sysInfoBattVy);
    tft_print_P(PSTR("N/A"));

    // Percent N/A
    tft_fillRect(sysInfoValX, sysInfoBattPctY, sysInfoValW, h, C_BLACK);
    tft_setCursor(sysInfoValX, sysInfoBattPctY);
    tft_print_P(PSTR("N/A"));

    return;
  }

  float v = getBatteryVolts();
  int16_t mv = (int16_t)(v * 1000.0f + 0.5f);
  uint8_t pct = batteryPercentFromVolts(v);

  if (!force && mv == lastSysBattMv && pct == lastSysBattPct) return;

  lastSysBattMv = mv;
  lastSysBattPct = pct;

  // Battery Voltage value (2 decimals per SSOT rule)
  tft_fillRect(sysInfoValX, sysInfoBattVy, sysInfoValW, h, C_BLACK);
  tft_setCursor(sysInfoValX, sysInfoBattVy);
  tft_setTextColor(C_WHITE, C_BLACK);
  printVoltage2dp(v);
  tft_print_P(PSTR(" V"));

  // Battery % value
  tft_fillRect(sysInfoValX, sysInfoBattPctY, sysInfoValW, h, C_BLACK);
  tft_setCursor(sysInfoValX, sysInfoBattPctY);
  tft_setTextColor(C_WHITE, C_BLACK);
  tft_print_int(pct);
  tft_print_char('%');

}

static void serviceSystemInfoLive() {
  if (screen != SCR_SYSTEM_INFO) return;
  const uint32_t now = millis();
  if ((int32_t)(now - sysInfoDueMs) < 0) return;
  sysInfoDueMs = now + 500;
  updateSystemInfoBatteryBox(false);
}

static void serviceAutoRescan() {
  if (screen != SCR_GRAPH) return;           // paused in menu/screens
  if (sweepInProgress) return;               // don’t re-enter sweep

  const uint32_t iv = autoIntervalMs();
  if (!iv) return;                           // AR_NONE

  const uint32_t now = millis();
  if (autoNextDueMs == 0) {
    autoNextDueMs = now + iv;                // initialize
    return;
  }

  if ((int32_t)(now - autoNextDueMs) >= 0) {
    if (!isCalibratedNow()) {
      showCalibrationNeededPopup();
      autoNextDueMs = now + iv;              // full interval after popup
      return;
    }
    runMeasurementStub();                    // blocking sweep (your current model)
    autoNextDueMs = millis() + iv;           // full interval after sweep finishes
  }
}

static void clearPlotAreaOnly() {
  if (!gGraphGeomValid) return;
  // Clear ONLY the inner graph rectangle (where the curve is drawn)
  tft_fillRect(gGX0, gGY0, (int16_t)(gGX1 - gGX0 + 1), (int16_t)(gGY1 - gGY0 + 1), C_BLACK);
}


// ============================================================
// Encoder polling (UNCHANGED)
// ============================================================
static inline void encoderPoll() {
  const uint32_t now = millis();
  if ((int32_t)(now - encPollDueMs) < 0) return;
  encPollDueMs = now + 1;

  uint8_t a = digitalRead(ENC_A);
  uint8_t b = digitalRead(ENC_B);
  uint8_t ab = (a << 1) | b;

  if (ab == encLastAB) return;
  encLastAB = ab;

  encState = ttable[encState & 0x0F][ab];
  uint8_t result = encState & 0x30;

  if (result == DIR_CW)  encDetentDelta++;
  if (result == DIR_CCW) encDetentDelta--;
}

static void xtalTuneOutputEnter() {
  if (xtalTuneActive) return;

  // Save current output state so we can restore on exit.
  xtalPrevF0 = aaGetFreq(0);
  xtalPrevF1 = aaGetFreq(1);

  // Apply current quartz reference, then output 10 MHz on CLK0, CLK1 disabled.
  aaSetRefXtal(quartzHz);
  (void)aaSetFreq(10000000L, 0L);

  xtalTuneActive = true;
}

static void xtalTuneOutputExit() {
  if (!xtalTuneActive) return;

  // Restore whatever outputs were active before tuning (0 disables output).
  (void)aaSetFreq((int32_t)xtalPrevF0, (int32_t)xtalPrevF1);

  xtalTuneActive = false;
}

// ============================================================
// enterScreen() — NORMALIZED FSM ENTRY
// ============================================================
static void enterScreen(Screen s) {

  // If we are leaving the Quartz tuning screen, restore normal output
  if (screen == SCR_XTAL_FREQ && s != SCR_XTAL_FREQ) {
    xtalTuneOutputExit();
  }
  
  screen = s;

  // ---------- common pre-entry ----------
  menuEditMode = false;     // safe default
  calUiDirty   = false;

  // ---------- GRAPH ----------
  if (s == SCR_GRAPH) {
    tft_fillScreen(C_BLACK);
    drawGraphScreen();

    if (graphDataInvalid) {
      clearPlotAreaOnly();                 // remove old curve pixels
      // restore x-axis baseline if your sweep draws a green progress line on it
      tft_drawFastHLine(gX_AXIS, gY_AXIS, (int16_t)(tft_width() - gX_AXIS), C_WHITE);
      graphDataInvalid = false;
    }

    // force battery redraw
    lastBattMvShown  = -1;
    lastBattPctShown = 255;
    lastBattChargingShown = false;

    // Auto rescan scheduling rules (SSOT rev24)
    const uint32_t iv = autoIntervalMs();

    if (iv && isCalibratedNow()) {
      if (firstGraphEntryAfterBoot) {
        // Boot: do initial sweep immediately, then start countdown
        firstGraphEntryAfterBoot = false;
        runMeasurementStub();
        autoScheduleFromNowFullInterval();
      } else {
        // Return from menu/screens: schedule after full interval (no immediate sweep)
        autoScheduleFromNowFullInterval();
      }
    } else {
      // Disabled or not calibrated
      autoNextDueMs = 0;
      firstGraphEntryAfterBoot = false; // prevent repeated “boot-immediate” attempts
    }

    return;
  }

  
  // ---------- MAIN MENU ----------
  if (s == SCR_MAIN_MENU) {

    menuSel = 0;
    bandDirty = false;   // OK only if bandDirty is declared
    drawMainMenuEx();
    return;
  }


  // ---------- SYSTEM INFO ----------
  if (s == SCR_SYSTEM_INFO) {
    drawSystemInfoStatic();
    return;
  }

  // ---------- USER BAND SETTINGS ----------
  if (s == SCR_USER_BAND_SETTINGS) {
    tft_fillScreen(C_BLACK);

    drawTopBar(PSTR("Menu->User Band"));

    ubSel = 0;

    // reset edit state on entry
    ubEditMode = false;
    ubEditItem = UB_EDIT_NONE;
    drawHelpLine_None();

    for (uint8_t i = 0; i < UB_N; i++) {
      drawUserBandRow(i, (i == (uint8_t)ubSel));
    }
    return;
  }

  // ---------- QUARTZ FREQUENCY ----------
  if (s == SCR_XTAL_FREQ) {
    // sart output of 10Mhz on CLK0 and ANT
    xtalTuneOutputEnter();

    tft_fillScreen(C_BLACK);

    drawTopBar(PSTR("Menu->Quartz"));

    // reset submenu selection + edit state on entry
    xtalSel = 0;
    xtalEditMode = false;
    xtalEditItem = XTAL_EDIT_NONE;

    drawHelpLine_Quartz();

    for (uint8_t i = 0; i < 3; i++) {
      drawXtalRow(i, (i == (uint8_t)xtalSel));
    }
    return;
  }

  // ---------- CALIBRATION ----------
  if (s == SCR_CALIBRATION) {
    calSel = 0;
    drawCalibrationAll();
    return;
  }
 
  // ---------- RESET CONFIRM ----------
  if (s == SCR_RESET_CONFIRM) {
    tft_fillScreen(C_BLACK);
    rstSel = RST_NO;
    drawResetConfirmAll();
    return;
  }

}

static void handleDetents(int16_t detents) {
  if (detents == 0) return;

  // ============================================================
  // MAIN MENU (SSOT rev20)
  // - Edit mode supports:
  //   * Band (MI_FIELD)
  //   * SWR Range (MI_FIELD)
  //   * Battery V Cal. (special)
  //   * Quartz Frequency (special)
  // ============================================================
  if (screen == SCR_MAIN_MENU) {

    // IMPORTANT: must match mainMenuEx[] order
    //const uint8_t MM_BAND      = 0;
//    const uint8_t MM_USER_BAND = 1;
//    const uint8_t MM_SWR_RANGE = 2;
    const uint8_t MM_BATT_CAL  = MR_BATT_CAL;

    // const uint8_t MM_RESET   = 5;
    // const uint8_t MM_SYSINFO = 6;
    // const uint8_t MM_BACK    = 7;

    // ---------- EDIT MODE ----------
    if (menuEditMode) {

      // --- Battery V Cal. (special edit) ---
      if ((uint8_t)menuSel == MM_BATT_CAL) {

        // 0.005 V per detent
        float v = calTargetVolts + (float)detents * BATT_CAL_STEP_V;

        if (v < BATT_CAL_V_MIN) v = BATT_CAL_V_MIN;
        if (v > BATT_CAL_V_MAX) v = BATT_CAL_V_MAX;

        if (v == calTargetVolts) return;

        calTargetVolts = v;

        // Update working coefficient using frozen raw reference
        if (battCalRawRefV < 0.10f) battCalRawRefV = 0.10f;
        battCalScale = calTargetVolts / battCalRawRefV;

        redrawMainMenuRow((uint8_t)menuSel, true);
        drawBatteryCalPanel(MAIN_EX_N);
        return;
      }

      // --- Generic MI_FIELD edit (Band / SWR Range) ---
      MenuItemEx& it = mainMenuEx[menuSel];

      if (it.type == MI_FIELD && it.value) {
        int16_t v = (int16_t)(*it.value) + detents;

        if (v < it.minVal) v = it.minVal;
        if (v > it.maxVal) v = it.maxVal;

        if (v == *it.value) return;

        *it.value = (int8_t)v;

        // Special handling for Band (affects derived band range label)
        if (it.value == (int8_t*)&currentBand) {
          bandDirty = true;
          invalidateGraphData();

        }

        if (it.value == (int8_t*)&swrRangeSel) {
          invalidateGraphData();   // <<< ADD (optional “immediate” behavior)
        }

        redrawMainMenuRow((uint8_t)menuSel, true);
      }
      return;
    }

    // ---------- NAVIGATION MODE ----------
    int8_t oldSel = menuSel;
    menuSel += detents;

    while (menuSel < 0)          menuSel += MAIN_EX_N;
    while (menuSel >= MAIN_EX_N) menuSel -= MAIN_EX_N;

    if (oldSel == menuSel) return;

    redrawMainMenuRow((uint8_t)oldSel, false);
    redrawMainMenuRow((uint8_t)menuSel, true);
    updateMainMenuHelpLine();
    return;
  }

  // ============================================================
  // QUARTZ FREQUENCY MENU (SCR_XTAL_FREQ)
  // ============================================================
  if (screen == SCR_XTAL_FREQ) {

    // ----- EDIT MODE -----
    if (xtalEditMode) {
      const uint32_t oldProg = xtalCalProgHz;
      const uint32_t oldCand = xtalCalCandidateHz;
      const uint8_t  oldStep = xtalStepSel;

      if (xtalEditItem == XTAL_EDIT_FREQ) {
        const uint32_t stepHz = xtalStepHz();

        int32_t v = (int32_t)xtalCalProgHz + (int32_t)detents * (int32_t)stepHz;

        if (v < (int32_t)XTAL_CAL_PROG_MIN_HZ) v = (int32_t)XTAL_CAL_PROG_MIN_HZ;
        if (v > (int32_t)XTAL_CAL_PROG_MAX_HZ) v = (int32_t)XTAL_CAL_PROG_MAX_HZ;

        xtalCalProgHz = (uint32_t)v;

        // Program CLK0 only (keep CLK1 disabled)
        (void)aaSetFreq((int32_t)xtalCalProgHz, 0L);

        // Compute candidate XTAL:
        // X_candidate = X_base * target / programmed
        uint64_t num = (uint64_t)xtalCalBaseHz * (uint64_t)XTAL_CAL_TARGET_HZ;
        xtalCalCandidateHz = (uint32_t)((num + (xtalCalProgHz / 2u)) / (uint64_t)xtalCalProgHz);

        // Clamp to allowed XTAL range
        if (xtalCalCandidateHz < XTAL_MIN) xtalCalCandidateHz = XTAL_MIN;
        if (xtalCalCandidateHz > XTAL_MAX) xtalCalCandidateHz = XTAL_MAX;

      } else if (xtalEditItem == XTAL_EDIT_STEP) {
        int16_t v = (int16_t)xtalStepSel + (int16_t)detents;
        while (v < 0) v += (int16_t)XTAL_STEP_N;
        while (v >= (int16_t)XTAL_STEP_N) v -= (int16_t)XTAL_STEP_N;
        xtalStepSel = (uint8_t)v;
      }

      if (oldProg == xtalCalProgHz && oldCand == xtalCalCandidateHz && oldStep == xtalStepSel) return;

      drawXtalRow((uint8_t)xtalSel, true);
      return;
    }


    // ----- NAV MODE -----
    int8_t oldSel = xtalSel;
    xtalSel += detents;

    while (xtalSel < 0)   xtalSel += 3;
    while (xtalSel >= 3)  xtalSel -= 3;

    if (oldSel == xtalSel) return;

    drawXtalRow((uint8_t)oldSel, false);
    drawXtalRow((uint8_t)xtalSel, true);
    return;
  }

  // ============================================================
  // CALIBRATION SCREEN (SSOT 17.12)
  // - Encoder: select Cancel / Start
  // ============================================================
  if (screen == SCR_CALIBRATION) {
    int8_t oldSel = calSel;
    calSel += detents;

    if (calSel < 0) calSel = 0;
    if (calSel >= (int8_t)CAL_N) calSel = (int8_t)(CAL_N - 1);

    if (oldSel == calSel) return;

    // Redraw only the affected rows
    drawMenuItem(100 + (int16_t)oldSel * 20, calItems[(uint8_t)oldSel], false);
    drawMenuItem(100 + (int16_t)calSel  * 20, calItems[(uint8_t)calSel],  true);
    return;
  }

  // ============================================================
  // USER BAND SETTINGS MENU
  // ============================================================
  if (screen == SCR_USER_BAND_SETTINGS) {

    // ----- EDIT MODE -----
    if (ubEditMode) {
      const uint32_t oldLo   = userBandLoHz;
      const uint32_t oldHi   = userBandHiHz;
      const uint8_t  oldStep = ubStepSel;

      const uint32_t stepHz = ubStepHz();

      if (ubEditItem == UB_EDIT_START) {
        // Start: clamp to [UB_MIN_HZ .. min(Stop-stepHz, UB_MAX_HZ-stepHz)]
        int32_t v = (int32_t)userBandLoHz + (int32_t)detents * (int32_t)stepHz;

        uint32_t minV = UB_MIN_HZ;
        uint32_t maxV = (userBandHiHz > UB_MIN_HZ + stepHz) ? (userBandHiHz - stepHz) : UB_MIN_HZ;
        if (maxV > (UB_MAX_HZ - stepHz)) maxV = (UB_MAX_HZ - stepHz);

        if (v < (int32_t)minV) v = (int32_t)minV;
        if (v > (int32_t)maxV) v = (int32_t)maxV;

        userBandLoHz = (uint32_t)v;

      } else if (ubEditItem == UB_EDIT_STOP) {
        // Stop: clamp to [Start+stepHz .. UB_MAX_HZ]
        int32_t v = (int32_t)userBandHiHz + (int32_t)detents * (int32_t)stepHz;

        uint32_t minV = userBandLoHz + stepHz;
        uint32_t maxV = UB_MAX_HZ;

        if (v < (int32_t)minV) v = (int32_t)minV;
        if (v > (int32_t)maxV) v = (int32_t)maxV;

        userBandHiHz = (uint32_t)v;

      } else if (ubEditItem == UB_EDIT_STEP) {
        // Step cycles through 10k / 100k / 1M (wrap)
        int16_t v = (int16_t)ubStepSel + (int16_t)detents;
        while (v < 0) v += (int16_t)UB_STEP_LIST_N;
        while (v >= (int16_t)UB_STEP_LIST_N) v -= (int16_t)UB_STEP_LIST_N;
        ubStepSel = (uint8_t)v;
      }

      if (oldLo == userBandLoHz && oldHi == userBandHiHz && oldStep == ubStepSel) return;

      // redraw currently selected row (value changes)
      drawUserBandRow((uint8_t)ubSel, true);
      return;
    }

    // ----- NAV MODE -----
    int8_t oldSel = ubSel;
    ubSel += detents;

    while (ubSel < 0)     ubSel += UB_N;
    while (ubSel >= UB_N) ubSel -= UB_N;

    if (oldSel == ubSel) return;

    drawUserBandRow((uint8_t)oldSel, false);
    drawUserBandRow((uint8_t)ubSel, true);
    return;
  }

  // ============================================================
  // RESET CONFIRM
  // ============================================================
  if (screen == SCR_RESET_CONFIRM) {
    int8_t old = rstSel;
    rstSel += detents;

    while (rstSel < 0)      rstSel += RST_N;
    while (rstSel >= RST_N) rstSel -= RST_N;

    if (old == rstSel) return;

    for (uint8_t i = 0; i < RST_N; i++) {
      int16_t y = 100 + (int16_t)i * 20;
      drawMenuItem(y, resetItems[i], (i == (uint8_t)rstSel));
    }
    return;
  }

  // ============================================================
  // GRAPH SCREEN
  // ============================================================
  if (screen == SCR_GRAPH) {
    ensureCursorValid(currentBand);

    uint16_t oldX = cursorX_byBand[currentBand];

    int32_t v = (int32_t)oldX + (int32_t)detents;
    if (v < 0) v = 0;
    if (v > (GRAPH_W - 1)) v = (GRAPH_W - 1);

    uint16_t newX = (uint16_t)v;
    if (newX == oldX) return;

    cursorX_byBand[currentBand] = newX;

    // update cursor overlay (fast)
    moveGraphCursor(oldX, newX);

    // Update derived values + topbar (fast)
    syncCursorDerived();
    drawTopBarValues(TB_UPD_FREQ | TB_UPD_SWR_CURR);

    // If auto sweep is enabled, defer the next sweep while the user is moving the cursor.
    if (autoIntervalMs() != 0) {
      autoScheduleFromNowFullInterval();
    }
    return;
  }
}

static void handleClick(bool isLongPress) {

  // ============================================================
  // GRAPH SCREEN (SSOT)
  // - Click: measurement stub
  // - Long press: enter main menu
  // ============================================================
  if (screen == SCR_GRAPH) {
    if (isLongPress) {
      enterScreen(SCR_MAIN_MENU);
    } else {
      runMeasurementStub();
      // Manual sweep also restarts the auto timer for a full interval
      autoScheduleFromNowFullInterval();
    }
    return;
  }

  
  // ============================================================
  // SYSTEM INFO (SSOT)
  // - Click: return to main menu
  // - Long press: return to main menu (safe)
  // ============================================================
  if (screen == SCR_SYSTEM_INFO) {
    (void)isLongPress;
    enterScreen(SCR_MAIN_MENU);
    return;
  }

  // ============================================================
  // QUARTZ FREQUENCY MENU (SCR_XTAL_FREQ)
  // ============================================================
  if (screen == SCR_XTAL_FREQ) {

    // ----- Edit mode -----
    if (xtalEditMode) {

      if (isLongPress) {
        // SAVE
        if (xtalEditItem == XTAL_EDIT_FREQ) {
          // Commit computed candidate
          quartzHz = xtalCalCandidateHz;

          aaSetRefXtal(quartzHz);
          if (quartzHz != quartzHzOrig) eepromSave();

          // After updating XTAL, re-program a clean 10 MHz for verification
          xtalCalBaseHz      = quartzHz;
          xtalCalProgHz      = XTAL_CAL_TARGET_HZ;
          xtalCalCandidateHz = quartzHz;
          (void)aaSetFreq((int32_t)xtalCalProgHz, 0L);
        }

        // step is RAM-only
      } else {
        // CANCEL
        if (xtalEditItem == XTAL_EDIT_FREQ) {
          // Do NOT change quartzHz; just reset output and computed display
          xtalCalProgHz      = XTAL_CAL_TARGET_HZ;
          xtalCalCandidateHz = quartzHzOrig;
          (void)aaSetFreq((int32_t)xtalCalProgHz, 0L);
        } else if (xtalEditItem == XTAL_EDIT_STEP) {
          xtalStepSel = xtalStepOrigSel;
        }
      }

      xtalEditMode = false;
      xtalEditItem = XTAL_EDIT_NONE;

      drawHelpLine_None();
      drawXtalRow((uint8_t)xtalSel, true);
      return;
    }

    // ----- Navigation mode -----
    if (isLongPress) return;

    if ((uint8_t)xtalSel == XTAL_BACK) {
      enterScreen(SCR_MAIN_MENU);
      return;
    }

    if ((uint8_t)xtalSel == XTAL_FREQ) {
      quartzHzOrig = quartzHz;

      // Base is the currently-applied reference (already applied on screen entry)
      xtalCalBaseHz      = quartzHz;
      xtalCalProgHz      = XTAL_CAL_TARGET_HZ;
      xtalCalCandidateHz = quartzHz;

      // Re-center output at 10 MHz at the moment editing begins
      (void)aaSetFreq((int32_t)xtalCalProgHz, 0L);

      xtalEditMode = true;
      xtalEditItem = XTAL_EDIT_FREQ;

      drawHelpLine(PSTR("LP: Save   Click: Cancel"));
      drawXtalRow((uint8_t)xtalSel, true);
      return;
    }

    if ((uint8_t)xtalSel == XTAL_STEP) {
      xtalStepOrigSel = xtalStepSel;
      xtalEditMode = true;
      xtalEditItem = XTAL_EDIT_STEP;

      drawHelpLine(PSTR("LP: Save   Click: Cancel"));
      drawXtalRow((uint8_t)xtalSel, true);
      return;
    }

    return;
  }

  // ============================================================
  // MAIN MENU (SSOT rev20)
  // - Band + SWR Range: click enters edit; LP saves; click cancels
  // - Battery V Cal + Quartz Frequency: click enters edit; LP saves; click cancels
  // - Other actions: click enters
  // ============================================================
  if (screen == SCR_MAIN_MENU) {

    // IMPORTANT: these indices must match your mainMenuEx[] order
//    const uint8_t MM_BAND      = 0;
    const uint8_t MM_USER_BAND = MR_USER_BAND;
//  const uint8_t MM_SWR_RANGE = 2;
    const uint8_t MM_BATT_CAL  = MR_BATT_CAL;

//    const uint8_t MM_RESET     = 5;
    // const uint8_t MM_SYSINFO = 6;
    // const uint8_t MM_BACK    = 7;

    MenuItemEx& it = mainMenuEx[menuSel];

    // ----- Edit mode (per selected row) -----
    if (menuEditMode) {

      // Battery V Cal edit (special)
      if ((uint8_t)menuSel == MM_BATT_CAL) {

        if (isLongPress) {
          // SAVE: recompute K using current uncal voltage to avoid rawRef drift
          const float u = battVoltsUncal;
          if (u > 0.10f) {
            battCalScale = calTargetVolts / u;
          }

          if (battCalScale != battCalScaleOrig) eepromSave();

          // Re-anchor EMA so display does not "jump" on exit
          battVoltsEma = calTargetVolts;
          battInit = true;
        } else {
          // CANCEL
          battCalScale = battCalScaleOrig;
        }

        bandDirty = false;
        menuEditMode = false;

        drawHelpLine_None();
        clearMenuExtraArea(MAIN_EX_N);
        redrawMainMenuRow((uint8_t)menuSel, true);
        updateMainMenuHelpLine();
        return;
      }


      // Default edit mode: MI_FIELD (Band / SWR Range, etc.)
      if (isLongPress) {
        const bool changed = (*(it.value) != menuEditOrig);

        if (changed) {
          // Persist only fields that belong in EEPROM
          if (it.value == (int8_t*)&currentBand ||
             it.value == (int8_t*)&swrRangeSel ||
             it.value == (int8_t*)&autoRescanSel) {
            eepromSave();
          }
          // GRAPH INVALIDATION TRIGGERS (new)
          if (it.value == (int8_t*)&currentBand ||
              it.value == (int8_t*)&swrRangeSel) {
            invalidateGraphData();          // <<< ADD
          }

        }
      } else {
        // Cancel
        *(it.value) = menuEditOrig;
      }

      bandDirty = false;
      menuEditMode = false;

      drawHelpLine_None();
      redrawMainMenuRow((uint8_t)menuSel, true);
      updateMainMenuHelpLine();
      return;
    }

    // ----- Normal mode -----
    if (!isLongPress) {

      // User Band submenu
      if ((uint8_t)menuSel == MM_USER_BAND) {
        enterScreen(SCR_USER_BAND_SETTINGS);
        return;
      }

      // Battery V Cal: enter edit mode (special)
      if ((uint8_t)menuSel == MM_BATT_CAL) {

        // Snapshot for Cancel
        battCalScaleOrig = battCalScale;

        // Start from current displayed battery volts
        calTargetVolts = getBatteryVolts();
        if (calTargetVolts < 3.00f) calTargetVolts = 3.00f;
        if (calTargetVolts > 4.25f) calTargetVolts = 4.25f;

        // Freeze raw reference for stable coefficient computation while rotating
        battCalRawRefV = battVoltsUncal;
        if (battCalRawRefV < 0.10f) battCalRawRefV = 0.10f;

        battCalScale = calTargetVolts / battCalRawRefV;

        bandDirty = false;
        menuEditMode = true;

        drawHelpLine(PSTR("LP: Save   Click: Cancel"));
        redrawMainMenuRow((uint8_t)menuSel, true);
        drawBatteryCalPanel(MAIN_EX_N);
        return;
      }

  
      // Standard MI_FIELD edit entry (Band / SWR Range)
      if (it.type == MI_FIELD) {
        menuEditOrig = *(it.value);
        bandDirty = false;
        menuEditMode = true;

        drawHelpLine(PSTR("LP: Save   Click: Cancel"));
        redrawMainMenuRow((uint8_t)menuSel, true);
        return;
      }

      // Standard MI_ACTION click
      if (it.type == MI_ACTION) {

        // Avoid entering SCR_NONE for rows implemented as "special" above
        if ((uint16_t)it.nextScreen != (uint16_t)SCR_NONE) {
          enterScreen(it.nextScreen);
        }
      }
      return;
    }

    return;
  }

  // ============================================================
  // USER BAND SETTINGS MENU (SSOT 17.6)
  // ============================================================
  if (screen == SCR_USER_BAND_SETTINGS) {

    // ----- EDIT MODE -----
    if (ubEditMode) {

      if (isLongPress) {
        // Save
        const uint32_t stepHz = ubStepHz();

        ubEditMode = false;
        ubEditItem = UB_EDIT_NONE;
        drawHelpLine_None();

        // Safety enforce (should already be clamped in handleDetents)
        if (userBandLoHz < UB_MIN_HZ) userBandLoHz = UB_MIN_HZ;
        if (userBandLoHz > (UB_MAX_HZ - stepHz)) userBandLoHz = (UB_MAX_HZ - stepHz);

        if (userBandHiHz > UB_MAX_HZ) userBandHiHz = UB_MAX_HZ;
        if (userBandHiHz <= userBandLoHz + stepHz) userBandHiHz = userBandLoHz + stepHz;

        // If Start/Stop were changed -> persist + recenter USER cursor index
        const bool changedStartStop = (userBandLoHz != ubLoOrigHz) || (userBandHiHz != ubHiOrigHz);
        if (changedStartStop) {
          cursorX_byBand[BAND_USER] = (uint16_t)(GRAPH_W / 2);
          eepromSave(); // persist USER band range + per-band cursor index
          invalidateGraphData();
        }

        // Step is RAM-only: do NOT save EEPROM if only Step changed

        drawUserBandRow((uint8_t)UB_START, ((UserBandRow)ubSel == UB_START));
        drawUserBandRow((uint8_t)UB_STOP,  ((UserBandRow)ubSel == UB_STOP));
        drawUserBandRow((uint8_t)UB_STEP,  ((UserBandRow)ubSel == UB_STEP));
        return;
      }

      // Click = Cancel
      userBandLoHz = ubLoOrigHz;
      userBandHiHz = ubHiOrigHz;
      ubStepSel    = ubStepOrigSel;

      ubEditMode = false;
      ubEditItem = UB_EDIT_NONE;
      drawHelpLine_None();

      drawUserBandRow((uint8_t)UB_START, ((UserBandRow)ubSel == UB_START));
      drawUserBandRow((uint8_t)UB_STOP,  ((UserBandRow)ubSel == UB_STOP));
      drawUserBandRow((uint8_t)UB_STEP,  ((UserBandRow)ubSel == UB_STEP));
      return;
    }

    // ----- MENU MODE -----
    if (isLongPress) return;

    switch ((UserBandRow)ubSel) {

      case UB_START:
      case UB_STOP:
      case UB_STEP:
        ubEditMode = true;
        ubEditItem = (UserBandEditField)ubSel; // 0/1/2 align with UB_EDIT_*
        ubLoOrigHz = userBandLoHz;
        ubHiOrigHz = userBandHiHz;
        ubStepOrigSel = ubStepSel;
        drawHelpLine(PSTR("LP: Save   Click: Cancel"));
        drawUserBandRow((uint8_t)ubSel, true);
        return;

      case UB_RESET:
        // SSOT: reset USER-band cursor index to center and persist
        cursorX_byBand[BAND_USER] = (uint16_t)(GRAPH_W / 2);
        eepromSave();
        return;

      case UB_BACK:
      default:
        enterScreen(SCR_MAIN_MENU);
        return;
    }
  }

  // ============================================================
  // CALIBRATION SCREEN (SSOT 17.12)
  // - Click on Cancel: exit to main menu
  // - Long press on Start: run calibration sweep (stores ZeroLevel table)
  // ============================================================
  if (screen == SCR_CALIBRATION) {

    if (!isLongPress) {
      if ((CalRow)calSel == CAL_CANCEL) enterScreen(SCR_MAIN_MENU);
      return;
    }

    // Long press
    if ((CalRow)calSel != CAL_START) {
      enterScreen(SCR_MAIN_MENU);
      return;
    }

    // Show a simple "running" screen (blocking calibration).
    tft_fillScreen(C_BLACK);
    drawTopBar(PSTR("Calibration"));

    tft_setTextSize(2);
    tft_setTextColor(C_WHITE, C_BLACK);
    tft_setCursor(10, 34);
    tft_print_P(PSTR("Running..."));

    tft_setTextSize(1);
    tft_setCursor(10, 68);
    tft_print_P(PSTR("Keep antenna connector open"));

    const bool ok = aaCalibrateZeroLevel();

    tft_fillScreen(C_BLACK);
    drawTopBar(PSTR("Calibration"));

    tft_setTextSize(2);
    tft_setTextColor(C_WHITE, C_BLACK);
    tft_setCursor(10, 34);
    if (ok) tft_print_P(PSTR("Done"));
    else    tft_print_P(PSTR("FAILED"));

    tft_setTextSize(1);
    tft_setCursor(10, 70);
    if (ok) tft_print_P(PSTR("Calibration saved to EEPROM"));
    else    tft_print_P(PSTR("No changes were saved"));

    delay(900);

    enterScreen(SCR_MAIN_MENU);
    return;
  }

  // ============================================================
  // RESET CONFIRM (SSOT path)
  // - Cancel returns to Main menu
  // ============================================================
  if (screen == SCR_RESET_CONFIRM) {

    if (!isLongPress) {
      if ((ResetChoice)rstSel == RST_NO) enterScreen(SCR_MAIN_MENU);
      return;
    }

    if ((ResetChoice)rstSel == RST_YES) {
      resetToDefaultsNow();
      enterScreen(SCR_MAIN_MENU);
    }
    return;
  }
}

// ============================================================
// setup()
// ============================================================
void setup() {
  initSoftLatchPins();

//  Serial.begin(9600);



#if DEBUG
  DBG_BEGIN(9600);
  DBG_PRINTLN();
  DBG_PRINT("FW ");
  DBG_PRINT(FW_VER_MAJOR);
  DBG_PRINT(".");
  if (FW_VER_MINOR < 10) DBG_PRINT("0");
  DBG_PRINTLN(FW_VER_MINOR);
  DBG_PRINT(F("[BOOT] quartzHz="));  
  DBG_PRINTLNF((float)quartzHz, 0);

#endif

#if BOOT_ZEROLEVEL_CAL

  // Minimal init: EEPROM + Si5351 only
  eepromLoad();
  aaInit(quartzHz);  // init Si5351 with current quartz reference
  delay(50);

#if DEBUG
  DBG_PRINTLN("[BOOT] ZeroLevel calibration (boot-only)");
#endif

  const bool ok = aaCalibrateZeroLevel();

#if DEBUG
  DBG_PRINT("[BOOT] ZeroLevel result: ");
  DBG_PRINTLN(ok ? "OK" : "FAIL");
#endif

  // Ensure outputs are OFF
  aaSetFreq(0, 0);

  // Halt here (no UI / no loop activity)
  while (1) {
    // optional: keep soft-latch logic alive if you want long-press power-off to still work
    serviceSoftLatch();
    delay(50);
  }

#endif


  eepromLoad();
  aaInit(quartzHz);      // init Si5351 with current quartz reference
  initBatterySense();

  pinMode(ENC_A, INPUT);
  pinMode(ENC_B, INPUT);
  pinMode(ENC_SW, INPUT);

  encState = R_START;

  // --- TGLIB init ---
  tglib_init();

  // --- Default colors ---
  tglib_fore = 0xFFFF;   // white
  tglib_back = 0x0000;   // black


  tglib_fore = 0xFFFF;   // white
  tglib_back = 0x0000;   // black


  enterScreen(SCR_GRAPH);   // 🔒 START SCREEN

}



// ============================================================
// loop()  — CLEAN FSM VERSION (long press via handleClick)
// ============================================================
void loop() {
  // ================== services ==================
  serviceSoftLatch();
  serviceBatterySense();
  serviceSystemInfoLive();
  serviceAutoRescan();


  // ================== encoder ==================
  encoderPoll();


  int8_t det;
  noInterrupts();
  det = encDetentDelta;
  encDetentDelta = 0;
  interrupts();

  if (det != 0) {
    handleDetents(det);
  }


  // ================== top bar ==================
  drawTopBar(NULL);

  // ================== button handling ==================
  const uint32_t now = millis();
  const bool swReleased = (digitalRead(ENC_SW) != LOW);  // active LOW

  static bool btnPrev = true;
  static uint32_t pressStartMs = 0;
  static bool longPressHandled = false;

  // -------- state change (debounced) --------
  if (swReleased != btnPrev) {
    if ((uint32_t)(now - btnLastChangeMs) >= BTN_DEBOUNCE_MS) {
      btnLastChangeMs = now;
      btnPrev = swReleased;

      // ===== button pressed =====
      if (!swReleased) {
        pressStartMs = now;
        longPressHandled = false;
      }
      // ===== button released =====
      else {
        uint32_t pressDur = now - pressStartMs;

        if (!longPressHandled && pressDur < BTN_LONGPRESS_MS) {
          // SHORT CLICK
          handleClick(false);
        }
      }
    }
  }

  // -------- long press detection (while held) --------
  if (!swReleased && !longPressHandled) {
    if ((uint32_t)(now - pressStartMs) >= BTN_LONGPRESS_MS) {
      longPressHandled = true;

#if DEBUG
      DBG_PRINTLN("[BTN] long press");
#endif
      // LONG CLICK
      handleClick(true);
    }
  }

}

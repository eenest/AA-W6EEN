

# Arduino Antenna Analyser (AA-W6EEN) — SSOT
> **Status:** AUTHORITATIVE  
> **Last update:** **01/19/2026 (rev 24)**  
> **Scope:** UI bring-up + power + battery + EEPROM + sweep / graph UI + RF generator abstraction  
> **Out of scope:** RF accuracy, bridge linearity

Preserved blocks are historical reference only and are not normative.

---

## 1. Scope (v1)
- Minimum UI + measurements scaffolding to validate workflow.
- Persistent settings in EEPROM.
- Graph screen + cursor + SWR range selection.

---


- Bring up **power + TFT + encoder + power button (soft-latch) + battery voltage meter**
- Implement **UI, menus, graph rendering, cursor behavior, sweep visualization**
- Implement **frequency sweep engine skeleton** (timing + buffering)
- RF measurement math is out of scope, but **RF generator integration is in scope**

Rule: Avoid hard-coded numeric indices for UI/menu selection indices and screen IDs (use enums/constants), but numeric constants are allowed for rendering/math.

---


### 1.1. SWR / return loss fundamentals


#### 1.1.1 Definitions
- **Γ (Gamma)**: reflection coefficient magnitude.
- **Return Loss (dB)**: `RL = 20*log10(Γ)`
- **SWR**: `SWR = (1+Γ)/(1-Γ)`

#### 1.1.2 Notes on sign / validity
- Γ must be in `[0..1)` for SWR to be valid.
- If Γ ≥ 1, SWR is undefined/infinite.

#### 1.1.3 Pico-SWR host math background
The legacy Pico-SWR host program uses:

- `Gamma = sqrt(Data)*1000 / ZeroLevel[freq]`
- `ReturnLoss = 20 * log10(Gamma)`
- `SWR = (1+Gamma)/(1-Gamma)`

And calibrates:

- `ZeroLevel[freq] = sqrt(Data)*1000`  (no load)

#### 1.1.4 SWR calculation
`Data` is the number measured and produced by the firmware (see §20).

Calibration values are stored in EEPROM as a ZeroLevel table (float). The table is **not** kept in RAM and must only be accessed via `getZeroLevel(frequencyHz)` (see §22).

Host/device math (authoritative):

- `Gamma = sqrt(Data) * 1000 / getZeroLevel(frequencyHz)`
- `ReturnLoss = 20 * log10(Gamma)` *(optional; sign convention note may apply)*
- `SWR = (1 + Gamma) / (1 - Gamma)`

ZeroLevel calibration (no load connected):

- `ZeroLevel(bucketFreq) = sqrt(Data) * 1000`

Where `bucketFreq` is the ZeroLevel bucket selected internally by the ZeroLevel module based on `CAL_STEP_HZ` (see §22.2). External code must not assume the bucket math or step size.

---

### 2. Hardware (high-level)


#### 2.1 MCU + Display
- MCU: **Arduino Nano (ATmega328P)**
- Display: **ST7789 320x240**
- UI input: **rotary encoder** with integrated push button

#### 2.2 TFT wiring (FINAL, rev 14)
 #define TFT_CS    10   // SPI hardware SS (recommended)
 #define TFT_DC     8
 #define TFT_RST    9

Notes:

TFT_CS intentionally moved to D10 (hardware SS) to guarantee SPI master mode.

Prevents accidental SPI slave fallback.

Frees D7 for future expansion.

Verified working with minimal Tiny Graphics test sketch.

Graphics library migration (rev 14)

Adafruit_GFX removed (flash/RAM constraints).

Using minimal ST7789 Tiny Graphics driver (Technoblogy-style).

No frame buffer, no virtual transforms.

Final display scan configuration:

 #define ST7789_MADCTL 0x08   // BGR only, native scan direction

No MX / MY / MV bits used.

Panel native orientation matches PCB mounting.

#### 2.3 RF Generator
- Frequency generator abstraction is in scope
- Generator implementation is Si5351

---


## 3. Hardware (v1 assumptions)
- MCU: ATmega328P class (Arduino Nano old bootloader)
- Display: ST7789 320x240
- Rotary encoder with click/LP
- RF generator: Si5351

#### 3.1 Pins (FINAL)
- `D5` = **PWR_LATCH**
  - Boot: set `HIGH` ASAP to latch power
  - Power-off: set `LOW`, then `pinMode(D5, INPUT)` (Hi-Z)
- `D6` = **PWR_BTN_SENSE**
  - Button shorts to GND → reads **LOW**
  - Firmware uses `INPUT` (no internal pull-up)

#### 3.2 Shutdown rule (FINAL)
If `D6` remains **LOW for ≥ 3 seconds**:
1. Display centered boxed overlay **“Shutting Down”**
2. Wait until button is released (`D6` HIGH)
3. Cut power by releasing `D5`

Wait is implemented using millis() / polling (no delay()).

#### 3.3 Timing rule (FINAL)
- **No `delay()` anywhere**
- Use `millis()` for timing and UI responsiveness

#### 3.4. Battery voltage measurement

Battery sense ADC pin must not overlap AA_ADC_PIN; define a separate BATT_ADC_PIN (or state which pin is used).


#### 3.5 Hardware (🔒 LOCKED)
- Battery voltage measured via analog input (ADC)
- Calibration coefficient applied in firmware to produce adjusted voltage

#### 3.6 UI display rules (v1)
- Battery voltage displayed with **2 decimals** (where voltage is shown numerically, e.g. System Info / Battery screens)
- Top bar shows **battery icon only** (right side, always visible)



#### 3.7 EEPROM persistence (high-level)
- Settings struct with signature + CRC.
- Separate “ZeroLevel EEPROM region” stored at end-of-EEPROM (see §22).

---

### 4. UI / Display


#### 4.1 Top bar (global)
- Top bar visible on all screens

- Right side: **battery icon only** (reserved width; no numeric voltage in v1)

- Left side: “topbar content area”, used in one of two modes:

- **Title mode:** firmware prints a single string (screen title / breadcrumb)

- **Free mode:** if title is NULL, firmware leaves left area unchanged so the active screen (e.g. Graph) may draw its own live values in the top bar area

#### 4.1.1 Top bar helper contract (v1)
- drawTopBar(const char* titleOrNull)

- If titleOrNull != NULL: draws top bar background, prints title if non-empty, and updates battery icon.

- If titleOrNull == NULL: updates battery icon only and does not modify the left area or the background.

- Graph screen entry uses drawTopBar("") once to clear the bar (no title), then renders its own top-bar content; loop calls drawTopBar(NULL) to keep battery fresh.

##### 4.1.2 Battery reserved width (implementation invariant, v1)
- Firmware defines a global constant `TOPBAR_BATT_W` (pixels) representing the reserved top-bar width on the right for the battery icon.
- All top-bar text (titles, breadcrumbs, or graph values) must render within `0 .. (tft.width() - TOPBAR_BATT_W)` and must not overwrite the battery area.
- `TOPBAR_BATT_W` must be derived from battery icon geometry (e.g., `BATT_ICON_W`) plus margins/padding (no magic numbers scattered in code).


---

#### 4.2 Fonts
- Use minimal font set (Technoblogy / Tiny Graphics font is 6x8)

---

### 5. Encoder & Button


#### 5.1 Hardware rule (FINAL)
- Encoder inputs have external pull-ups to **3.3V**
- Internal pull-ups should be **disabled** in final firmware

---

## 6. Graph screen (SSOT)


### 6.1 Graph screen layout
- Top bar (frequency, band, battery)
- Plot area
- Cursor
- Bottom helper line

### 6.1.3 Graph top-bar content (SCR_GRAPH, free mode, v1)
When the cursor is moved, the top bar is updating:
- `F=<freq>`
  - `<freq>` is rendered in MHz with 4 decimals, e.g. `7.1234 MHz`.

- Battery area on the right.


---

## 7. Encoder interaction (SSOT)
- Detents: rotate
- Click: action
- Long press: exit/back

---

## 8. Measurement stubs (v1)
- Graph screen click triggers measurement stub.

---

## 9. ZeroLevel Calibration screen
See §17.12 (SCR_CALIBRATION).

---

## 10. System info
- Shows firmware version.

---

## 11. Band model


<details>
<summary><strong>Rev21 version of this section (preserved)</strong></summary>

- Built-in bands list (160m..10m etc.)
- User Band: startHz/stopHz stored in EEPROM
- Band selection stored as last_band_id

---

</details>

### 11.1 Device bandwidth (v1)
- Device sweep/calibration bandwidth: **1.000–60.000 MHz**
- USER band must always be within **1.000–60.000 MHz** (see §17.5).
- ZeroLevel calibration sweep (see §22) covers the full device bandwidth (no partial tables).

### 11.2 Built-in (FCC-defined) HF/VHF bands within 1–60 MHz
Built-in bands are defined as contiguous frequency ranges in Hz. Names are short (“160m”, “80m”, …).

> Note on 60m: FCC rules for 60m are channel-based (center frequencies). For sweep/UI purposes in v1,
> we define a single “60m” band range matching the allocation limits, and (optionally) can constrain
> transmit/marker features elsewhere to the allowed channels.
> 60m is channelized (and now also includes a contiguous sub-band per FCC action); for v1 sweep/UI we show a ‘60m’ band as a convenience range

Built-in bands (Hz):

| Band | Start (Hz) | Stop (Hz) | Notes |
|---|---:|---:|---|
| 160m | 1800000 | 2000000 | |
| 80m  | 3500000 | 4000000 | |
| 60m  | 5330500 | 5406400 | Allocation limits; channel centers are defined by FCC (see below) |
| 40m  | 7000000 | 7300000 | |
| 30m  | 10100000 | 10150000 | |
| 20m  | 14000000 | 14350000 | |
| 17m  | 18068000 | 18168000 | |
| 15m  | 21000000 | 21450000 | |
| 12m  | 24890000 | 24990000 | |
| 10m  | 28000000 | 29700000 | |
| 6m   | 50000000 | 54000000 | |

60m channel centers (kHz) (informational reference):
- 5332.0, 5348.0, 5358.5, 5373.0, 5405.0

Implementation rule:
- Band selection uses an enum/ID list and is stored as `last_band_id` in EEPROM.
- Cursor index is stored per band (including USER).
- When a built-in band is selected, the active sweep range is `[band.startHz .. band.stopHz]`.

### 11.3 USER band
- USER band is a contiguous `[startHz .. stopHz]` range configured in §17.5.
- USER band start/stop are stored in EEPROM.
- USER band step (10 kHz / 100 kHz / 1 MHz) is RAM-only and defaults to 100 kHz on boot/reset.

---


## 13. Graph buffer storage (SWR sweep)
- Stored as centi-SWR in RAM buffer.

---

## 14. Cursor
- Stored per band.

---

## 15. SWR ranges
- 1..2.5, 1..5, 1..10

---

## 16. Main menu (SSOT)
- Data-driven menu items.

---


## 17. Menu System (AUTHORITATIVE)


### 17.1 Common Principles (Menu Interaction Model)


#### 17.1.1 Encoder & Button rules (global)


##### SCR_GRAPH (main working screen)
- **Rotate**: move the graph cursor.
  - Every detent moves the cursor by **1 dot** (left/right).
  - When the cursor moves, the top bar updates per §6.1.3 (SCR_GRAPH free mode).
- **Click**: start (or restart) a sweep and redraw the graph.
- **Long press**: enter menu mode → `SCR_MAIN_MENU`.
- This MUST work even while a sweep is in progress: on long press during sweep, abort the sweep safely (stop ADC/Timer, stop RF outputs, clear sweepInProgress) and then switch to the menu (see §18.15).

##### Menu mode (any menu screen)
- **Rotate**: move selection.
- **Click**: activate selected item.
  - If selected item is an editable field `<value>` → enter **edit mode**.
  - If selected item is a submenu/action → enter/execute it.
  - If selected item is `Back` → go up / exit menu mode.
- **Long press**: no action by default (reserved for explicit exceptions defined in SSOT).

##### Edit mode (editing `<value>`)
- **Rotate**: change value.
- **Long press**: **Save** and exit edit mode.
- **Click**: **Cancel** (revert to original value) and exit edit mode.

To exit menu mode (or go up the menu hierarchy) user must select the `Back` item.

#### 17.1.2 Edit mode (field editing)
Editable fields are shown as: `<value>` (or `<curr_value>`)

In edit mode:
- **Rotate** encoder → adjust value (step depends on field)
- **Long press** → Save
- **Click** → Cancel (revert)

During edit mode show hint at bottom:
- `LP: Save   Click: Cancel`

#### 17.1.3 Common “Back” behavior
- Every menu (and sub-menu) ends with `Back`
- Exiting a menu screen is done via selecting `Back` (no “click anywhere to exit”)

#### 17.1.4 Numeric constraints (global)
- If user tries to go beyond allowed range → clamp to limits
- If two fields have dependency (Start/Stop) → enforce SSOT rules
- If editable field is a list of predefined values → cycle the values

#### 17.1.5 Visual style (🔒 LOCKED)
All menus share the same **visual navigation affordance**:
- A **selector marker** (`>`) at the far-left of the menu row for the currently-selected item.
- The label text is shifted right by 1 character to keep alignment clean.

### 17.2 Screen IDs (FSM-friendly, 🔒 LOCKED)
> These IDs are locked to keep the state machine stable across refactors.

- `SCR_BOOT` (splash with some info — to be done later)
- `SCR_GRAPH` (default working screen)
- `SCR_MAIN_MENU`
- `SCR_SYSTEM_INFO`
- `SCR_USER_BAND_SETTINGS`
- `SCR_RESET_CONFIRM`
- `SCR_XTAL_FREQ`
- `SCR_CALIBRATION`



### 17.3 Startup & global behavior
- On boot: briefly display splash screen with some info (TBD)
- Default screen after boot: `SCR_GRAPH`
- Long press from `SCR_GRAPH` opens `SCR_MAIN_MENU`
- Battery icon and top bar are visible on all screens
- If autoRescanSel != None is loaded from EEPROM (see §17.6.1) and calibration is valid, the firmware triggers an initial sweep immediately on first entry to SCR_GRAPH, then starts the interval countdown for the next sweep.

### 17.4 Main Menu (SCR_MAIN_MENU)
Main Menu (rendered rows)
- `Band <name> <start>..<end> MHz`     *(editable: select band)*
- `User Band <start>-<end> MHz`        *(submenu)*
- `SWR Range <value>`                  *(editable: `1..10`, `1..5`, `1..2.5`)*
- `Auto Rescan <value>`                *(editable: None, 10 sec, 30 sec ; persisted in EEPROM; default None)*
- `Battery V Cal. <value> V`           *(editable)*
- `Quartz Frequency <value>`           *(submenu)*
- `Calibration`                        *(submenu)*
- `Reset to Defaults`                  *(action)*
- `System Info`                        *(screen)*
- `Back`

Band display format (rendered row):
- `> Band <name> <start>..<end> MHz`

Documentation-only marker:
- `|-` is used in SSOT trees only and is not rendered on-screen.

Additional UI rule (User Band row):
- `User Band <start>-<end> MHz` shows **MHz with 3 decimals** for both Start/Stop (example: `User Band 1.000-60.000 MHz`).

Behavior:
- `Band ...`
  - Click → enter edit mode
  - Rotate → select band (detent step = 1 band)
  - Long press (in edit) → Save band and exit edit mode
  - Click (in edit) → Cancel band edit (revert) and exit edit mode

- `User Band <start>-<end> MHz`
  - Click → open `SCR_USER_BAND_SETTINGS`

- `SWR Range <value>` *(editable; persisted in EEPROM)*
  - Click → enter edit mode (see 17.6)

- `Auto Rescan <value>` *(editable; persisted in EEPROM)*

Click → enter edit mode (see 17.6.1)

- `Battery V Cal. <value> V` *(editable; persisted in EEPROM)*
  - Click → enter edit mode (see 17.10.1)

- `Quartz Frequency <value>` *(submenu)*
  - Click →  open `SCR_XTAL_FREQ`

- `Calibration` *(submenu)*
  - Click →  open `SCR_CALIBRATION`

- `Reset to Defaults`
  - Click → open `SCR_RESET_CONFIRM` (see 17.10.2)

- `System Info`
  - Click → enter `SCR_SYSTEM_INFO`

- `Back`
  - Click → return to `SCR_GRAPH`

### 17.5 User Band Settings (SCR_USER_BAND_SETTINGS)
User Band (menu)
- `Start Frequency <value>`      *(editable)*
- `Stop Frequency <value>`       *(editable)*
- `Step <value>`                 *(editable: `10 kHz`, `100 kHz`, `1 MHz`)*
- `Reset Cursor to Center`       *(action)*
- `Back`

#### Values / formatting
- Start/Stop are shown as **MHz with 3 decimals** (example: `3.500 MHz`).
- Step is shown as one of: `10 kHz`, `100 kHz`, `1 MHz`.

#### Editing rules
- Allowed absolute range for both Start/Stop: **1.000–60.000 MHz**
- Let `stepHz` be the currently selected Step:
  - `Start Frequency < Stop Frequency − stepHz`
  - `Stop Frequency > Start Frequency + stepHz`

#### Interaction model
- Rotate: moves selection (in navigation mode).
- Click on an editable row (`Start Frequency`, `Stop Frequency`, `Step`) → enter edit mode.
- While in edit mode:
  - Rotate:
    - Start/Stop change by `stepHz`.
    - Step cycles through the list (`10 kHz` → `100 kHz` → `1 MHz` → wrap).
  - Long press → **Save**
    - Applies constraint rules (clamps to valid range and enforces minimum separation based on `stepHz`).
    - Exits edit mode and stays on `SCR_USER_BAND_SETTINGS`.
    - If Start/Stop were changed: reset **USER-band cursor position** to the middle of the new range.
  - Click → **Cancel**
    - Reverts the edited value to the original.
    - Exits edit mode and stays on `SCR_USER_BAND_SETTINGS`.

- UI hint while in edit mode (bottom line):
  - `LP: Save   Click: Cancel`

#### Actions
- `Reset Cursor to Center`:
  - Click → resets the **USER-band** cursor/marker position to the middle of the current USER range.
  - Cursor position is persisted (cursor index is stored in EEPROM).

- `Back`:
  - Click → return to `SCR_MAIN_MENU`.

#### Persistence
- Start/Stop are stored in EEPROM:
  - `userBand.startHz`
  - `userBand.stopHz`
- Step is **RAM only** and defaults to `100 kHz` on boot and after “Reset to Defaults”.

### 17.6 SWR Range editable item (in Main Menu)
- Allowed values: `1..10`, `1..5`, `1..2.5`
- Click on `SWR Range <value>` → enter edit mode
- Rotate → cycle allowed values
- Long press → Save
- Click → Cancel

Persistence:
- Stored in EEPROM as `swrRangeSel` (enumerated selection).
- On boot:
  - If EEPROM valid → load `swrRangeSel` from EEPROM.
  - If EEPROM invalid / fresh system → default to `1..10`.
- On “Reset to Defaults” → set to default `1..10` and write to EEPROM.

Representation (implementation guidance):
- `swrRangeSel` is a small enum/index:
  - `0` → `1..10`
  - `1` → `1..5`
  - `2` → `1..2.5`

### 17.6.1 Auto Rescan (Main Menu)

**Purpose:** automatically re-run a sweep on the graph screen at a fixed interval.

**UI location:** Main Menu, **immediately below** **SWR Range**.

**Options (cycled in edit mode):**
- `None` (default)
- `10 sec`
- `30 sec`

**Persistence:** this setting **MUST be saved to EEPROM** and restored on boot.

**Recommended representation (firmware):**
- `enum AutoRescanSel : uint8_t { AR_NONE=0, AR_10S=1, AR_30S=2 };`
- `AutoRescanSel autoRescanSel;` (EEPROM-backed)
- `uint32_t autoRescanNextMs;` (next scheduled sweep time in millis; RAM-only)
- `bool autoRescanBootImmediate;` (true only right after boot if the first sweep must run immediately)

**Timing rules:**
- If `autoRescanSel != AR_NONE` **on boot**, then after EEPROM load, the firmware **MUST trigger an initial sweep immediately** when entering `SCR_GRAPH`, and then start the countdown to the next sweep.
- While any non-graph screen is active (menus, system info, calibration, reset confirm), **auto rescan is paused**.
- When returning to `SCR_GRAPH` from any other screen, the next automatic sweep is scheduled **after a full interval** (i.e., no immediate sweep on return).

**Interaction rules:**
- User long-press to enter the main menu **MUST work even while a sweep is in progress** (see §17.1.1 / §18.15). When long-press is detected during a sweep, the sweep must be aborted safely and the UI must switch to the menu.


### 17.7 Frequency Display (v1)
- Frequency axis display mode is **always Linear** in v1.
- There is **no menu item** for frequency display mode.
- No EEPROM storage applies.

### 17.8 System Info (SCR_SYSTEM_INFO)
System Info (screen content, not a menu)

Header:
- `Simple Antenna Analyser`
- *(empty line)*
- `Design by W6EEN`
- `Based on the Pico-SWR from UR5FFR`
- *(empty line)*
- `Built on <date/time>`

Fields:
- Firmware Version
- EEPROM Status
- Battery Voltage
- Battery %
- Battery Adj. (Battery adjustment multiplier - battCalScale)
- Quartz Frequency (real measured Si5351 quartz frequency)

Hint at the bottom of the screen:
- `Click: return to the menu`

Behavior:
- Click → return to `SCR_MAIN_MENU`

### 17.9 EEPROM responsibilities (Menu-related)


Stored:
- `last_band_id`
- `cursorIndex` **per band**
- `userBand.startHz`
- `userBand.stopHz`
- `battCalScale` (battery adjustment multiplier)
- `quartzFrequencyHz` (Si5351 reference / quartz frequency in Hz)
- `swrRangeSel` (SWR Range selection: `1..10` / `1..5` / `1..2.5`)
- `autoRescanSel` (Auto Rescan selection: None / 10 sec / 30 sec)

Not stored:
- Menu state
- Active selection

Invariants / safety rules:
- No blocking calls in UI/FSM paths
- No recursive screen transitions
- All edit modes are explicit states
- Long press is **never ambiguous**

Reset rule:
- Reset to Defaults resets all stored settings to defaults **except** `quartzFrequencyHz` (preserved).
- On reset, `battCalScale` is set to default `1.0000f`.
- On reset, `swrRangeSel` is set to default `1..10`.
- On reset, `autoRescanSel` is set to default `None`.

Cursor indices:
- Cursor indices are stored per band; invalid sentinel is `0xFFFF` (if encountered → initialize to `GRAPH_W/2`).


Note (ownership split):
- `aa-code.ino` owns EEPROM persistence for `quartzFrequencyHz` (load/save).
- `swr-pico.cpp` owns the XTAL calibration helper logic and Si5351 programming, but does not write EEPROM for `quartzFrequencyHz`; it consumes the value via `aaInit(ref_xtal_hz)` and returns a calibrated value via `xtalCalDone()`.

### 17.10 Calibration & Reset items (in Main Menu)

**Note (Auto Rescan interaction):**
- While the Main Menu is open, **Auto Rescan is paused**.
- On return to `SCR_GRAPH` **or any manual sweep click**, restart the auto-rescan timer for a **full interval**.

Menu rows (rendered):
- `Battery V Cal. <value> V`     *(editable)*
- `Quartz Frequency <value>`     *(submenu)*
- `Calibration`                  *(submenu → SCR_CALIBRATION)*
- `Reset to Defaults`            *(action)*
- `Back`

Navigation:
- Rotate → move selection
- Click:
  - on `Battery V Cal. <value> V` → enter edit mode
  - on `Quartz Frequency <value>` → open `SCR_XTAL_FREQ`
  - on `Calibration` → open `SCR_CALIBRATION`
  - on `Reset to Defaults` → open `SCR_RESET_CONFIRM`
  - on `Back` → return to `SCR_GRAPH`

Edit-mode hint (bottom line, while editing any `<value>`):
- `LP: Save   Click: Cancel`

#### 17.10.1 Battery Voltage Calibration (editable menu line)
Purpose:
- Adjust `battCalScale` so the displayed battery voltage matches a known reference.

Display:
- Menu line shows calibrated voltage as: `Battery V Cal. <value> V` with **2 decimals**.
- While in edit mode, show a 2-line helper block near the bottom of the screen:
  - `Raw: <raw_voltage> V` *(2 decimals; raw/no adjustment; display-only)*
  - `K: <battCalScale>` *(5 decimals; display-only)*

Limits and step rules:
- Calibration target voltage is clamped to **3.00 V .. 4.25 V**.
- Encoder rotation changes `battCalScale` indirectly so the **internal** target changes by **0.005 V per detent**.
  - Because the main display is **2 decimals**, the visible change is typically **0.01 V per 2 detents**.

Edit interaction:
- Click on `Battery V Cal. <value> V` → enter edit mode
- In edit mode:
  - Rotate → adjust `battCalScale` indirectly (per step rules above)
  - Long press → Save `battCalScale` to EEPROM; exit edit mode; remain on `SCR_MAIN_MENU`
  - Click → Cancel (revert to original `battCalScale`); exit edit mode; remain on `SCR_MAIN_MENU`

Persistence:
- `battCalScale` is stored in EEPROM.
- On Reset to Defaults, `battCalScale` is set to `1.0000f`.

#### 17.10.2 Reset to Defaults confirmation (SCR_RESET_CONFIRM)
Reset to Defaults is invoked from `SCR_MAIN_MENU`.

Screen:
- `No`
- `Yes`

Rules:
- Default selection: `No`
- There is **no Back item** on this screen
- Click on `No` → return to `SCR_MAIN_MENU` (no changes)
- `Yes` requires **Long press** to execute reset:
  - Long press on `Yes` → reset EEPROM/settings to defaults, then return to `SCR_MAIN_MENU`
  - Click on `Yes` → no action (prevents mistakes)

Bottom help:
- `LP on YES: Reset   Click: Cancel`

### 17.11 Quartz Frequency (SCR_XTAL_FREQ)
Purpose:
- Set the Si5351 reference frequency used by the firmware for frequency generation accuracy.

Representation:
- Store quartz/reference frequency as **unsigned long** in **Hz** (`quartzFrequencyHz`).

Defaults and persistence:
- Default on fresh system (or invalid EEPROM): `25000000` (25 MHz). (QTZ_DEFAULT_HZ = 25000000UL;)
- Stored in EEPROM.
- Reset to Defaults does **not** reset `quartzFrequencyHz` (preserved).

Quartz Frequency (menu)
- `Quartz Frequency <value>`     *(editable)*  // show the current quartz frequency, default - QTZ_DEFAULT_HZ = 25000000UL;
- `Step <value>`                 *(editable: `1 Hz`, `10 Hz`, `100 Hz`, `1 kHz`, `10 kHz`)*
- `Back`                         returns to the SCR_MAIN_MENU

Edit interaction:
- Click on `Quartz Frequency <value>` → enter edit mode
- In edit mode:
  - Rotate → adjust, using current step 
  - Long press → Save to EEPROM; exit edit mode; remain on `SCR_XTAL_FREQ`
  - Click → Cancel (revert); exit edit mode; remain on `SCR_XTAL_FREQ`


#### Values / formatting
- Quartz Frequency is shown in Hz (no separators), e.g. 25000000
- Step is shown as one of: `1 Hz`, `10 Hz`, `100 Hz`, `1 kHz`, `10 kHz`. Default it `1 Hz`.

#### Editing rules
- Allowed absolute range for quartz frequency: **XTAL_MIN .. XTAL_MAX**

#define XTAL_MIN  24000000
#define XTAL_MAX  35000000
static const uint32_t QTZ_DEFAULT_HZ = 25000000UL;



#### Interaction model
- Rotate: moves selection (in navigation mode).
- Click on an editable row (`Quartz Frequency`, `Step`) → enter edit mode.
- While in edit mode:
  - Rotate:
    - If editing Quartz Frequency: change by stepHz (current Step value).
    - If editing Step: cycle (1 Hz → 10 Hz → 100 Hz → 1 kHz → 10 kHz → wrap).
  - Long press → Save
    - Applies constraint rules (clamps to valid range)
    - Exits edit mode and stays on `SCR_XTAL_FREQ`.
  - Click → Cancel
    - Reverts the edited value to the original.
    - Exits edit mode and stays on `SCR_XTAL_FREQ`.

- UI hint while in edit mode (bottom line):
  - `LP: Save   Click: Cancel`

#### Actions
- `Back`:
  - Click → return to `SCR_MAIN_MENU`.

#### Persistence
- Quartz Frequency is stored in EEPROM
- Step is **RAM only** and defaults to `1 Hz` on boot and after “Reset to Defaults”.

#### 17.12 Calibration (SCR_CALIBRATION)
Calibration is invoked from `SCR_MAIN_MENU`.

Screen:
Print message: "Disconnect antenna for the calibration (no load)"
- `Cancel`
- `Start`

Rules:
- Default selection: `Cancel`
- There is **no Back item** on this screen
- Click on `Cancel` → return to `SCR_MAIN_MENU` (no changes)
- `Start` requires **Long press** to execute calibration:
  - Long press on `Start` → initiating the calibration process (collecting the ZeroLevel data and saving it to EEPROM), then return to `SCR_MAIN_MENU`
  - Click on `Start` → no action (prevents mistakes)

Bottom help:
- `LP Start: Calibrate, Click Cancel: Back`

- Calibration invalidates any existing ZeroLevel data at the start.
  - Rationale: we do not buffer a full calibration table in RAM; calibration writes results directly into the EEPROM region used by ZeroLevel.
- During calibration, ZeroLevel is considered invalid (measurements must return SWR=0 / “cannot measure”).
- On successful completion, ZeroLevel is marked valid.
- If calibration is interrupted or fails (power loss, reset, etc.), ZeroLevel remains invalid and the user must re-run calibration.
- Cancel serves as Back.

---

#### ✅ End of Menu SSOT section
---

### 18. Graph Rendering Spec (SCR_GRAPH) — Axes, Labels, Ticks, Cursor, Buffer


#### 18.1 Assumptions (v1)
- Display: **320×240**
- Font: **Technoblogy / Tiny Graphics 6×8**
  - `FW = 6` (font width, px)
  - `FH = 8` (font height, px)
- All layout sizes are derived from font size except constants tied to display dimensions and the fixed inner graph area (GRAPH_W, GRAPH_H) in 18.6.1.

- Top bar height is implementation constant `TOPBAR_H`.
- There must be at least **1 pixel gap** between top bar and graph area:
  - `TOPBAR_GAP = 1`

#### 18.2 Colors and stroke rules
- Graph background: **black**
- Axis + ticks + labels: **white**
- Graph curve: **yellow**
- Axis line width: **1 px**
- Tick line width: **1 px**
- Cursor: **white dashed** vertical line (pattern defined below) and dotted horizontal line (pattern defined below)

Overflow marker: red (C_RED).

#### 18.3 Coordinate model
- Screen origin: `(0,0)` is top-left
- `x` grows to the right, `y` grows downward.

Graph uses the “work area”:
- `WORK_Y0 = TOPBAR_H + TOPBAR_GAP`
- `WORK_Y1 = 239`

#### 18.4 Derived notch sizes
- SWR notch length (horizontal): `NOTCH_W = FW/2`  (integer division; for 6 px font ⇒ 3 px)
- Frequency notch height (vertical): `NOTCH_H = FW/2` (same as above)

#### 18.5 Axis placement (geometry)


##### 18.5.1 Vertical SWR axis X offset (from left edge)
Vertical axis must be offset from the left side by:

`X_AXIS = 1 + FW + 1 + NOTCH_W`

Meaning (left → right):
- `1 px` margin
- `FW` px reserved for SWR label text (single digit)
- `1 px` gap
- `NOTCH_W` px reserved for horizontal tick notch
- then the vertical axis line at `X_AXIS`

##### 18.5.2 Horizontal frequency axis Y offset (from bottom edge)
We must reserve bottom space for frequency labels and their notches.

Minimum bottom reservation:

`BOTTOM_RES = 1 + FH + 1 + NOTCH_H`

Horizontal axis line Y coordinate:

`Y_AXIS = 239 - BOTTOM_RES`

Meaning (bottom → top):
- `1 px` margin
- `FH` px reserved for frequency label text
- `1 px` gap
- `NOTCH_H` px reserved for vertical tick notch
- then the horizontal axis line at `Y_AXIS`

#### 18.6 Plot rectangle (where axes meet; background; tick zones)
Plot area excludes the axes strokes:

- `PLOT_X0 = X_AXIS + 1`
- `PLOT_X1 = 319`
- `PLOT_Y0 = WORK_Y0`
- `PLOT_Y1 = Y_AXIS - 1`

Derived:
- `PLOT_W = PLOT_X1 - PLOT_X0 + 1`
- `PLOT_H = PLOT_Y1 - PLOT_Y0 + 1`

#### 18.6.1 Fixed inner graph area (v1 simplification)
Inside `PLOT_*`, define a fixed “inner graph area” used for curve and cursor rendering:

- `GRAPH_W = 300`
- `GRAPH_H = 200`

Any remaining pixels within the plot rectangle are used as equal padding around the inner graph area:

- `PAD_X = (PLOT_W - GRAPH_W) / 2`
- `PAD_Y = (PLOT_H - GRAPH_H) / 2`

Inner graph area coordinates:
- `GX0 = PLOT_X0 + PAD_X`
- `GX1 = GX0 + GRAPH_W - 1`
- `GY0 = PLOT_Y0 + PAD_Y`
- `GY1 = GY0 + GRAPH_H - 1`

#### 18.7 Axis drawing rules
- Vertical axis: white line at `x = X_AXIS` from `y = PLOT_Y0` to `y = Y_AXIS`
- Horizontal axis: white line at `y = Y_AXIS` from `x = X_AXIS` to `x = 319`

#### 18.8 SWR axis: scale, ticks, and labels


##### 18.8.1 SWR ranges (from Settings → “SWR Range”)
Supported ranges:
- "1..10"  → max SWR = **10.0**
- "1..5"   → max SWR = **5.0**
- "1..2.5" → max SWR = **2.5**

SWR minimum is always **1.0** at the bottom, increasing upward.

##### 18.8.2 Tick spacing (notch frequency)
- In "1..10" mode: draw a notch every **0.5**
- In "1..5" mode: draw a notch every **0.2**
- In "1..2.5" mode: draw a notch every **0.1**

##### 18.8.3 Tick drawing
For each SWR tick at value `S`:
- Compute tick Y location inside **inner graph area** (mapping defined below)
- Draw a **horizontal notch** (white, 1 px tall) immediately left of the axis:
  - `x = (X_AXIS - NOTCH_W) .. (X_AXIS - 1)` at `y = tickY`

##### 18.8.4 Label policy (single digits only; no decimals)
- Labels must be **single digit** (no decimals, no multi-digit “10”)
- Label text is placed **to the left** of the notch region.
- Label should be **vertically centered** to the corresponding notch (centered on `tickY`).
- **Highest label is never printed** (even if a notch exists there).

Label sets:
- "1..10": print labels **1, 2, 3, 4, 5, 6, 7, 8, 9**
- "1..5":  print labels **1, 2, 3, 4**
- "1..2.5": print labels **1, 2** (do not print 2.5)

##### 18.8.5 SWR value → pixel mapping (inner graph area)
Let:
- `SWR_MIN = 1.0`
- `SWR_MAX = {10.0, 5.0, 2.5}` (depends on range)
- `SPAN = SWR_MAX - SWR_MIN`

Map SWR value `S` (clamped to `[SWR_MIN..SWR_MAX]`) to pixel Y:

- Normalize: `t = (S - SWR_MIN) / SPAN` in `[0..1]`
- Pixel Y:
  - `y = GY1 - round(t * (GRAPH_H - 1))`

So:
- `S = 1.0` maps to `GY1` (bottom of inner graph area)
- `S = SWR_MAX` maps to `GY0` (top of inner graph area)

If measured SWR is more than the MAX displayed SWR, the dot in the top of the screen (representing the overflow) should be painted RED

#### 18.9 Frequency axis: labels and ticks


##### 18.9.1 Label format
- Format: “#.##” (in MHz)
- No leading zero (e.g. `7.10`, not `07.10`)
- No "MHZ" is printed, just the value
- Labels are “aligned to the left” (string formatting without leading padding).
- **Highest frequency label is not printed.**
- Must print **at least 4 frequency labels per band**.

##### 18.9.2 Tick count and placement (aligned to inner graph area)
To satisfy “≥4 labels” while skipping the highest label:
- Default `FREQ_TICKS = 5` tick positions across the **inner graph width**.
  - This yields 4 printed labels (ticks 0..3) and skips tick 4 (highest).

Ticks are equally spaced in X across `GX0..GX1`:
- For tick index `i ∈ [0..FREQ_TICKS-1]`:
  - `x_i = GX0 + round(i * (GRAPH_W - 1) / (FREQ_TICKS - 1))`

For each tick position:
- Draw a **vertical notch** (white, 1 px wide) below the axis:
  - at `x = x_i`
  - `y = (Y_AXIS + 1) .. (Y_AXIS + NOTCH_H)`

##### 18.9.3 Tick frequency value mapping
For tick `i`:
- Band sweep range is `[F_LO..F_HI]` (Hz)
- `f_i = F_LO + i * (F_HI - F_LO) / (FREQ_TICKS - 1)`
- Convert to MHz and render as `XX.XX` (two decimals)

Printing rule:
- Print labels for `i = 0 .. FREQ_TICKS-2`
- Do **not** print label for `i = FREQ_TICKS-1` (highest)

##### 18.9.4 Label positioning
- Each frequency label is vertically placed in the bottom reserved label area (below the axis).
- Each label is horizontally **centered** on its tick notch **except** the leftmost label:
  - Leftmost (`i=0`) may be left-aligned to avoid clipping into the SWR axis area.

#### 18.10 Graph curve (yellow)
- Curve is drawn only inside inner graph area (`GX0..GX1`, `GY0..GY1`).
- Curve color: **yellow** (except overflow, see below)
- Graph X sampling domain uses inner width:
  - For graph column index `x ∈ [0..GRAPH_W-1]`, the corresponding frequency is:
    - `f(x) = F_LO + x * (F_HI - F_LO) / (GRAPH_W - 1)`

- The curve is drawn by iterating X columns `x ∈ [0..GRAPH_W-1]` and reading the corresponding sample:
  - `S_c = pack9_read_x100(x)`
  - If `S_c == 0`, the sample is invalid → do not plot, and break the polyline segment.

- Overflow behavior (matches current code):
  - If `S_c > MAX_c` for the active SWR range selection, the plotted Y is clamped to `MAX_c`,
    and the curve at that x is forced **red** (overflow indication).
- Pixel Y is derived from `S_c` using 18.12.1.

#### 18.11 Cursor (white)
Cursor consists of:
- vertical line at `cursorX`
- horizontal line at the derived `cursorY`

Cursor position:
- `cursorX ∈ [0..GRAPH_W-1]` (graph X index)
- `cursorY` is a **pixel Y** coordinate inside the inner-graph area.

How `cursorY` is obtained:
- If `pack9_read_x100(cursorX)` is **valid** (non-zero) → derive `cursorY` using the same mapping as the curve (18.12.1).
- If `pack9_read_x100(cursorX)` is **invalid** (zero) → do not draw the cursor horizontal line
  (vertical cursor line may still be drawn).

Bounds / drawing rule:
- `cursorY` must satisfy `GY0 ≤ cursorY ≤ (GY1 - 1)`
  - (we never draw at `GY1` to avoid overpainting the bottom axis line)

- Sentinel / invalid:
  - `pack9_read_x100(x) == 0` means “no measurement / invalid”
  - Invalid samples must not participate in min/max computations.

#### 18.12 SWR sweep buffer (authoritative data; used for rendering)
We do **not** store pixel Y coordinates. The authoritative stored sweep data is the packed SWR measurement buffer (`pack9ram`).

- Buffer length: `GRAPH_W` (v1: `GRAPH_W = 300`)
- Storage: packed 9-bit-per-sample bitstream in SRAM (`pack9ram`)
- Access API (authoritative):
  - `uint16_t pack9_read_x100(uint16_t x)`
  - `void     pack9_write_x100(uint16_t x, uint16_t swr_x100)`

- Representation: fixed-point centi-SWR (SWR ×100), **quantized to 0.05 SWR steps**
  - Valid returned range: `100..2650` representing `1.00..26.50`
  - Resolution: `5` in centi-SWR (0.05 SWR)

- Sentinel / invalid:
  - `pack9_read_x100(x) == 0` means “no measurement / invalid”
  - Invalid samples must not participate in min/max computations and must not be plotted.

- Write rules (canonical):
  - `pack9_write_x100(x, 0)` stores invalid
  - Non-zero values are clamped to `100..2650` and quantized to 0.05 steps before storage

##### 18.12.1 Sample-to-graph mapping (derive pixel Y) — integer / fixed-point
All mapping is performed in **centi-SWR** (SWR×100) integers.

Definitions:
- `S_c = pack9_read_x100(x)` (centi-SWR; returns `0` if invalid)
- `MIN_c = 100` (1.00)
- `MAX_c` depends on Settings → SWR Range:
  - 10.0 → `MAX_c = 1000`
  - 5.0  → `MAX_c = 500`
  - 2.5  → `MAX_c = 250`

Invalid sample:
- If `S_c == 0` → invalid; do not plot a point (and break the polyline segment).

Overflow marker:
- If `S_c > MAX_c`, the plotted point is clamped to `MAX_c` and rendered in **RED**.

Clamping:
- `S_plot_c = clamp(S_c, MIN_c, MAX_c)`

Mapping to pixel Y (inner graph area):
- `den = (MAX_c - MIN_c)`  (must be > 0)
- `num = (S_plot_c - MIN_c) * (GRAPH_H - 1)`
- `t = round(num / den)` using integer rounding:
  - `t = (num + den/2) / den`
- Pixel Y:
  - `y = GY1 - t`
  - (`y` is in `[GY1-(GRAPH_H-1) .. GY1]`)

Axis safety:
- When drawing curve/cursor, clamp final drawable Y to `≤ (GY1 - 1)` to avoid overpainting the bottom axis line.

#### 18.13 Invariants (v1)
- No graph pixels are drawn above `WORK_Y0`.
- There is always at least **1 px** blank gap between top bar and graph work area (`TOPBAR_GAP = 1`).
- Highest frequency label is never printed.
- Highest SWR label is never printed.
- Tick/label spacing derives from `FW` and `FH` only (plus 1 px stroke/margins).
- Curve drawing and cursor drawing are aligned to the **inner graph area** (`GRAPH_W × GRAPH_H`).
- PLOT_W ≥ GRAPH_W and PLOT_H ≥ GRAPH_H; otherwise clamp GRAPH_W/H to fit.
- Sweep buffer is `pack9_buf[]` (pack9ram) storing packed 9-bit samples, accessed via `pack9_read_x100()` / `pack9_write_x100()`.
- `pack9_read_x100(x) == 0` is the invalid/unmeasured sentinel.
- Graph pixel Y positions are derived from `pack9_read_x100()` at render time (no persistent Y buffer).

### 18.14 The graph cannot be displayed and band sweeps cannot be done if there is NO valid calibration data present.

When a band sweep is initiated (needed to draw the graph), and there is no valid calibration data present,
display a centered pop-up message "Device not calibrated" surrounded by a rectangle for visibility, then stop further processing.

### 18.15 Sweep abortability (required for Auto Rescan + Menu entry)

A sweep implementation **MUST be abortable** so that the user can enter the menu at any time.

**Requirements:**
- The sweep loop must be cooperative: it must periodically service the encoder/button state.
- A **long press** on the encoder switch while `sweepInProgress == true` must:
  1. Stop RF output / DDS/Si5351 output (`aaSetFreq(0,0)` or equivalent).
  2. Stop the sweep state (`sweepInProgress = false`).
  3. Restore the X-axis from the temporary progress color back to normal axis color.
  4. Leave any already-measured points in the SWR buffer intact (partial graph is allowed).
  5. Immediately transition to `SCR_MAIN_MENU`.

**UI rule:** it is acceptable if the cursor is temporarily overwritten by the sweep plot during the sweep; the cursor is redrawn only when the sweep finishes or when the user moves the cursor.

 
## 19. EEPROM settings
- Settings signature + CRC.

---

## 20. Pico-SWR measurement logic (data acquisition) — detailed


### 20.1 Measurement goal
The Pico-SWR measurement path returns a numeric value `Data` that represents the energy of an audio-frequency component (AF_FREQ) present in the sampled ADC stream. This is later transformed into Γ/SWR using the ZeroLevel calibration table.

### 20.2 Sampling overview
The firmware:

1) Generates an RF tone with Si5351.
2) Samples the detector output on AA_ADC_PIN (Arduino A0..A3 only) using the AVR ADC. Default - A0.
3) AA_ADC_PIN is a compile-time configuration (#define). A4/A5 are reserved for I2C (Si5351).
4) Uses a fixed-frequency Goertzel detector to estimate energy at `AF_FREQ` in the sampled stream.
5) Returns the real-part energy estimate as `Data` (float in the legacy implementation).

### 20.3 ADC + Timer triggering (how ADC works here)
This design uses **hardware auto-triggering** so ADC conversions run at a stable cadence:

- **Timer1** runs in **CTC** mode.
- **Timer1 Compare Match B** is selected as the ADC auto-trigger source.
- Every compare event triggers one ADC conversion.
- **ADC completion interrupt** (`ISR(ADC_vect)`) fires once per sample.

Key AVR registers (ATmega328P):

- `TCCR1A/TCCR1B/TCNT1/OCR1B/TIMSK1` configure Timer1 compare B interrupt/event.
- `ADMUX` selects input channel and voltage reference.
- `ADCSRA` enables ADC, sets prescaler, enables interrupt, enables auto-trigger.
- `ADCSRB` selects auto-trigger source (Timer1 Compare Match B).

### 20.4 Sampling rate
- Target AF tone: `AF_FREQ` (e.g., 1000 Hz)
- Sampling rate: `SAMPLING_FREQ = SAMPLING_K * AF_FREQ`

Example:
- `AF_FREQ = 1000`
- `SAMPLING_K = 16`
- `SAMPLING_FREQ = 16000 Hz`

Timer1 is configured so that Compare Match B happens at exactly 16 kHz (as close as possible given the MCU clock).

### 20.5 Why the multi-stage state machine exists
The ADC ISR implements a small state machine (`measure_stage`) to:

- **Stage 0**: Find a local minimum over ~1 AF cycle (helps suppress DC / slow drift)
- **Stage 1**: Find a local maximum over ~1 AF cycle (helps align phase)
- **Stage 2**: Run Goertzel accumulation for `N` samples
- **Stage 3**: Stop (measurement complete)

This structure provides a more stable result than starting accumulation at an arbitrary phase.

### 20.6 Goertzel details
Goertzel parameters:

- Window length: `N` samples (typical 256)
- Target bin `k = round(N * AF_FREQ / SAMPLING_FREQ)`
- Coefficient: `coeff = 2*cos(2*pi*k/N)`

Recurrence:

- `Q0 = coeff * Q1 - Q2 + sample`
- `Q2 = Q1`
- `Q1 = Q0`

Energy estimate (real part only; optimized form):

- `Data = Q1*Q1 + Q2*(Q2 - Q1*coeff)`

### 20.7 Output `Data`
- `Data` is positive.
- Larger `Data` => stronger detected AF component.

### 20.8 Measurement + Si5351 control module API (swr-pico)

This project provides a standalone module that contains **only**:
- SWR measurement acquisition (ADC + Timer1 + Goertzel, returning `Data`)
- Si5351 frequency output control
- ZeroLevel EEPROM subsystem (end-of-EEPROM float table; see §22)

This module intentionally does **not** include:
- `setup()`, `loop()`
- serial command parsing (`ParseCommand()` etc.)
- UI/menu logic

#### 20.8.1 Pin configuration (ADC)
- `AA_ADC_PIN` is a compile-time define selecting the ADC input pin.
- Allowed values: **A0..A3 only**
- A4/A5 are reserved for I2C (Si5351) and must not be used for ADC in v1.

#### 20.8.2 Initialization / stop

- XTAL/quartz ownership boundary (storage vs helpers):
  - EEPROM storage owner (host/UI): `aa-code.ino`
    - loads `quartzFrequencyHz` from EEPROM at boot,
    - saves `quartzFrequencyHz` to EEPROM when the user completes calibration,
    - passes the loaded value into the module via `aaInit(ref_xtal_hz)`.
  - Calibration helper + Si5351 programming (module): `swr-pico.cpp`
    - implements the XTAL calibration helper functions (`xtalStartCalibration()`, `xtalSetDelta()`, `xtalCalDone()`) and applies the provided XTAL to Si5351,
    - does **not** read/write `quartzFrequencyHz` in EEPROM (host owns persistence),
    - returns the calibrated XTAL value to the host (host decides whether/when to persist it).

- `void aaInit(uint32_t ref_xtal_hz);`
  - Enables / prepares the measurement subsystem.
  - Assumes Serial (if any) is already opened by the main code (module does not open Serial).
  - Sets the Si5351 reference XTAL frequency parameter to `ref_xtal_hz`.
  - If `ref_xtal_hz == 0`, the module forces a default XTAL frequency (25,000,000 Hz unless the generator backend defines a different default).

- `void aaStop(void);`
  - Ensures the ADC auto-trigger and ADC interrupt are disabled.
  - Ensures Timer1 Compare-B interrupt is disabled (no ADC trigger running).

#### 20.8.3 Si5351 output frequency control
- `bool aaSetFreq(int32_t clk0_hz, int32_t clk1_hz = 0);`
  - Sets output frequency for CLK0 and optional CLK1.
  - If `clkN_hz <= 0`, that output is disabled.
  - When disabling an output, firmware must:
    - disable it via output-enable mask (OE), AND
    - force the corresponding Si5351 CLK output driver to Hi-Z/off (PDN=1 in CLKx_CONTROL).
  - CLK2 is always disabled in this module.

- `uint32_t aaGetFreq(int clk = 0);`
  - Returns currently configured frequency for CLK0 (clk=0) or CLK1 (clk=1).
  - Returns `0` if that output is currently disabled.

#### 20.8.4 SWR read API (uses ZeroLevel)
- `float aaReadSWR(uint32_t frequencyHz);`
  - Reads `Data` at `frequencyHz` and computes SWR using §21 formulas.
  - If ZeroLevel is not valid (not calibrated), returns `0` to indicate SWR cannot be measured.

### 20.9 Si5351 XTAL calibration helper API (host workflow)

Goal:
- Because the generator backend uses a locked PLL multiplier, XTAL calibration is performed by:
  1) outputting a fixed calibration frequency `calFreq` on **CLK0** (default 10,000,000 Hz),
  2) letting the user measure the actual frequency externally,
  3) adjusting an internal “output delta” in Hz via UI steps,
  4) converting that delta into an updated XTAL frequency that the generator uses for its calculations.

Public helper API:

- `uint32_t xtalStartCalibration(uint32_t start_xtal_hz, uint32_t calFreq_hz);`
  - Returns the previously active XTAL frequency.
  - Forces CLK0 drive strength to **8 mA**.
  - Outputs `calFreq_hz` on CLK0 (if 0, default to 10,000,000 Hz).
  - Disables CLK1 (and CLK2 is always disabled).

- `uint32_t xtalSetDelta(int32_t step_out_hz);`
  - `step_out_hz` is a signed step applied to a host-visible **delta_out_hz** accumulator.
  - After applying the step, firmware recomputes the current XTAL frequency using:
    - `xtal_hz = base_xtal_hz + round(delta_out_hz * base_xtal_hz / calFreq_hz)`
  - Then re-programs CLK0 to the same `calFreq_hz` so the externally observed frequency changes.
  - Returns the new current XTAL frequency in Hz.

- `uint32_t xtalCalDone(void);`
  - Returns current XTAL frequency in Hz.
  - Stops CLK0 transmission (CLK0 disabled + output driver forced Hi-Z/off).
  - Restores normal drive strength policy.

Additional host-facing helpers (for UI/state):
- `uint32_t xtalSetOutOffsetAbs(int32_t offset_out_hz);`  (sets delta_out_hz absolutely)
- `bool     xtalCalIsActive(void);`
- `int32_t  xtalCalGetDeltaOutHz(void);`
- `uint32_t xtalCalGetCalFreqHz(void);`
- `uint32_t xtalGetCurrentHz(void);`

Persistence note (ownership): `xtalCalDone()` returns the calibrated XTAL frequency; `swr-pico.cpp` does not write EEPROM for this value. The host/UI (`aa-code.ino`) owns EEPROM persistence of `quartzFrequencyHz`.

---

## 21. Local SWR calculation (device-side)


### 21.1 Inputs
- `Data` from §20.
- `ZeroLevel` from EEPROM via `getZeroLevel(frequencyHz)`.

### 21.2 Steps
1) `zl = getZeroLevel(freqHz)`
2) If `zl == 0`, SWR is not computable (calibration missing) -> treat as invalid.
3) `Gamma = sqrt(Data) * 1000 / zl`
4) Clamp/guard:
   - If `Gamma < 0`, set to 0
   - If `Gamma >= 1`, SWR is infinite/invalid (implementation-defined handling)
5) `SWR = (1 + Gamma) / (1 - Gamma)`

### 21.3 Output format
- SWR is typically stored as `uint16_t` in centi-SWR (SWR×100) for graph buffers.

---

## 22. ZeroLevel EEPROM storage (float table; end-of-EEPROM; no RAM copy)


### 22.1 Requirements
- ZeroLevel data is allocated from the **end of EEPROM** to avoid clashes:
  - EEPROM map concept: `[<other saved data><unused space><zerolevel data>]`
- ZeroLevel table is **not** kept in RAM.
- External code must only use these APIs:
  - `saveZeroLevel(frequencyHz, value)`
  - `getZeroLevel(frequencyHz)`
  - `verifyZeroLevelData()`
  - `zeroLevel_factory_reset()`
  - `validateZeroLevelData()` *(finalize; makes table read-only)*
- The ZeroLevel bucket/index math and `CAL_STEP_HZ` are **internal** to the ZeroLevel module and must not be depended on by callers.
- Calibration does **not** allow partial tables:
  - A successful calibration produces exactly `CAL_COUNT` entries covering the full device bandwidth.
  - EEPROM header `count` must be exactly `CAL_COUNT` when valid.
- Write policy:
  - While invalid: `saveZeroLevel()` is allowed.
  - After `validateZeroLevelData()`: table becomes read-only; `saveZeroLevel()` must fail.
  - To recalibrate: user must call `zeroLevel_factory_reset()` first.
- Startup should read the ZeroLevel header and keep validity state in global/static variables.
- Calibration is an **atomic operation** (SSOT workflow):
  1) `zeroLevel_factory_reset()` (invalidates + clears region)
  2) sweep & measure across **1.000–60.000 MHz**
  3) for each bucket: compute `ZeroLevel = sqrt(Data)*1000` and call `saveZeroLevel(freqHz, ZeroLevel)`
  4) finalize with `validateZeroLevelData()` (writes header; becomes read-only)

<details>
<summary><strong>Rev21 version of this section (preserved)</strong></summary>

- ZeroLevel EEPROM logic is **separate** from existing XTAL EEPROM logic.
- ZeroLevel data is allocated from the **end of EEPROM** to avoid clashes:
  - EEPROM map concept: `[<other saved data><unused space><zerolevel data>]`
- No RAM copy of the table is required.
- Calibration writes are allowed only **until** the ZeroLevel table is validated.
- After validation, the table becomes **read-only** unless the whole ZeroLevel region is factory-reset.
- Startup should read the ZeroLevel header and keep **validity state** in global/static variables.
- Calibration should be performed as an **atomic operation**:
  1) reset/prepare (invalid table)
  2) sweep & measure
  3) save each entry to EEPROM
  4) validate table (write header)

</details>

### 22.2 Frequency range and indexing (step-hidden)
The calibration range and step size are defined by:

```cpp
#define CAL_MHZ_MIN  1u
#define CAL_MHZ_MAX  60u
#define CAL_STEP_HZ  1000000UL

#define CAL_MIN_HZ   ((uint32_t)CAL_MHZ_MIN * 1000000UL)
#define CAL_MAX_HZ   ((uint32_t)CAL_MHZ_MAX * 1000000UL)
#define CAL_COUNT    (((CAL_MAX_HZ - CAL_MIN_HZ) / CAL_STEP_HZ) + 1u)
```

**Contract (authoritative):**
- The rest of the firmware must **not** assume the bucket size (1 MHz, 0.5 MHz, etc.).
- The rest of the firmware must **not** compute indices; it must call `getZeroLevel()` and `saveZeroLevel()` with `frequencyHz`.

Index mapping is internal and uses `CAL_STEP_HZ`:

```cpp
static int16_t zl_index_from_freq(uint32_t frequencyHz)
{
  if (frequencyHz < CAL_MIN_HZ || frequencyHz > CAL_MAX_HZ) return -1;
  return (int16_t)((frequencyHz - CAL_MIN_HZ) / CAL_STEP_HZ);  // floor bucket; 0..CAL_COUNT-1
}

static uint32_t zl_bucket_freq_from_index(uint16_t idx)
{
  // For debug/UI/logging only
  return CAL_MIN_HZ + (uint32_t)idx * CAL_STEP_HZ;
}
```

### 22.3 EEPROM layout (end-of-EEPROM allocation)
The ZeroLevel EEPROM region is placed at the end of EEPROM using `E2END`.

Header format (4 bytes total):
- `sig`   (1 byte)  : signature marker (0 => invalid/uninitialized)
- `count` (1 byte)  : entry count; when valid it must be exactly `CAL_COUNT` (partial tables are not allowed)
- `crc`   (2 bytes) : checksum over **exactly** `CAL_COUNT * sizeof(float)` bytes of table data

Table format:
- `CAL_COUNT` entries of `float` (`sizeof(float)` bytes each)
- Entry value `0.0f` is reserved to mean “unset/invalid” (used only while unvalidated)

Total region size:

```cpp
#define ZL_HDR_BYTES      4u
#define ZL_ENTRY_BYTES    ((uint16_t)sizeof(float))
#define ZL_DATA_BYTES     ((uint16_t)CAL_COUNT * ZL_ENTRY_BYTES)
#define ZL_TOTAL_BYTES    (ZL_HDR_BYTES + ZL_DATA_BYTES)
```

Base address (start of region):

```cpp
#define ZL_EE_BASE        ((uint16_t)(E2END + 1u - ZL_TOTAL_BYTES))
#define ZL_EE_SIG_ADDR    ((uint16_t)(ZL_EE_BASE + 0u))
#define ZL_EE_COUNT_ADDR  ((uint16_t)(ZL_EE_BASE + 1u))
#define ZL_EE_CRC_ADDR    ((uint16_t)(ZL_EE_BASE + 2u))
#define ZL_EE_DATA_ADDR   ((uint16_t)(ZL_EE_BASE + 4u))
```

<details>
<summary><strong>Rev21 version of this section (preserved)</strong></summary>

The ZeroLevel EEPROM region is placed at the end of EEPROM using `E2END`.

Header format (4 bytes total):
- `sig`   (1 byte)  : signature marker (0 => invalid/uninitialized)
- `count` (1 byte)  : number of entries stored (expected `CAL_COUNT`)
- `crc`   (2 bytes) : checksum over `count * sizeof(float)` bytes of table data

Table format:
- `count` entries of `float` (`sizeof(float)` bytes each)

Total region size:

```cpp
#define ZL_HDR_BYTES      4u
#define ZL_ENTRY_BYTES    ((uint16_t)sizeof(float))
#define ZL_DATA_BYTES     ((uint16_t)CAL_COUNT * ZL_ENTRY_BYTES)
#define ZL_TOTAL_BYTES    (ZL_HDR_BYTES + ZL_DATA_BYTES)
```

Base address (start of region):

```cpp
#define ZL_EE_BASE        ((uint16_t)(E2END + 1u - ZL_TOTAL_BYTES))
#define ZL_EE_SIG_ADDR    ((uint16_t)(ZL_EE_BASE + 0u))
#define ZL_EE_COUNT_ADDR  ((uint16_t)(ZL_EE_BASE + 1u))
#define ZL_EE_CRC_ADDR    ((uint16_t)(ZL_EE_BASE + 2u))
#define ZL_EE_DATA_ADDR   ((uint16_t)(ZL_EE_BASE + 4u))
```

</details>

### 22.4 CRC/checksum definition (2 bytes)
To minimize flash size and avoid RAM usage, the SSOT defines the...**Fletcher-16** computed over the raw bytes of the float table data:

- `sum1 = 0`, `sum2 = 0`
- for each byte `b`:
  - `sum1 = (sum1 + b) % 255`
  - `sum2 = (sum2 + sum1) % 255`
- `crc = (sum2 << 8) | sum1`

Checksum coverage:
- Exactly `CAL_COUNT * sizeof(float)` bytes starting at `ZL_EE_DATA_ADDR`

<details>
<summary><strong>Rev21 version of this section (preserved)</strong></summary>

To minimize flash size and avoid RAM usage, the SSOT defines the 2-byte checksum as **Fletcher-16** computed over the raw bytes of the float table data:

- `sum1 = 0`, `sum2 = 0`
- for each byte `b`:
  - `sum1 = (sum1 + b) % 255`
  - `sum2 = (sum2 + sum1) % 255`
- `crc = (sum2 << 8) | sum1`

Checksum coverage:
- Exactly `count * sizeof(float)` bytes starting at `ZL_EE_DATA_ADDR`

</details>

### 22.5 Runtime state and write-protection policy
The ZeroLevel module keeps a cached state:
- `g_zl_valid` (bool): true if header + checksum validate
- `g_zl_count` (uint8): equals `CAL_COUNT` when valid, else 0

Initialization:
- To keep integration friction low (no extra public API), the module may **lazy-initialize** this cached state on the first call to any ZeroLevel API (`getZeroLevel()`, `saveZeroLevel()`, `validateZeroLevelData()`, etc.).

Write policy:
- While `g_zl_valid == false`, `saveZeroLevel()` is allowed.
- After successful `validateZeroLevelData()`, `g_zl_valid == true` and `saveZeroLevel()` must return false.
- To allow a new calibration, user action must call `zeroLevel_factory_reset()` first.

<details>
<summary><strong>Rev21 version of this section (preserved)</strong></summary>

At startup, firmware reads the EEPROM ZeroLevel header and sets:
- `g_zl_valid` (bool): true if header + checksum validate
- `g_zl_count` (uint8): stored entry count when valid, else 0

Write policy:
- While `g_zl_valid == false`, `saveZeroLevel()` is allowed.
- After successful `validateZeroLevelData()`, `g_zl_valid == true` and `saveZeroLevel()` must return false.
- To allow a new calibration, user action must call `zeroLevel_factory_reset()` first.

</details>

### 22.6 Required functions


#### 22.6.1 `void zeroLevel_factory_reset()`
Initializes/clears the ZeroLevel region in EEPROM:
- Sets signature = 0
- Sets count = 0
- Sets crc = 0
- Clears all `CAL_COUNT` float entries to `0.0f`

This makes the region **invalid** until `validateZeroLevelData()` is called.

#### 22.6.2 `bool verifyZeroLevelData()`
Returns:
- `true` only if ZeroLevel EEPROM region is valid
- `false` otherwise

Validity rules:
- signature byte matches the defined signature constant
- count is exactly `CAL_COUNT` (partial tables are not allowed)
- computed Fletcher-16 matches stored crc

<details>
<summary><strong>Rev21 version of this section (preserved)</strong></summary>

Returns:
- `true` only if ZeroLevel EEPROM region is valid
- `false` otherwise

Validity rules:
- signature byte matches the defined signature constant
- count is within `1..CAL_COUNT` (expected `CAL_COUNT` for 1–60 MHz)
- computed Fletcher-16 matches stored crc

</details>

#### 22.6.3 `bool saveZeroLevel(uint32_t frequencyHz, float value)`
Stores one ZeroLevel value into EEPROM.

Rules:
- Returns `false` if the ZeroLevel region is already valid (write-protected).
- Returns `false` if `frequencyHz` is outside `[CAL_MIN_HZ .. CAL_MAX_HZ]`.
- Indexing is internal (step-hidden) and uses §22.2 mapping; callers pass only `frequencyHz`.
- Stored value must be strictly `> 0.0f`. If `value <= 0.0f`, store `0.0f`.

Note: `saveZeroLevel()` writes only **one entry** and does **not** update crc.

<details>
<summary><strong>Rev21 version of this section (preserved)</strong></summary>

Stores one ZeroLevel value into EEPROM.

Rules:
- Returns `false` if the ZeroLevel region is already valid (write-protected).
- Returns `false` if `frequencyHz` is outside `[CAL_MIN_HZ .. CAL_MAX_HZ]`.
- Index is computed as the MHz bucket:
  - `mhz = floor(frequencyHz / 1e6)`
  - `idx = mhz - CAL_MHZ_MIN`
- Stored value must be strictly `> 0.0f`. If `value <= 0.0f`, store `0.0f`.

Note: `saveZeroLevel()` writes only **one entry** and does **not** update crc.

</details>

#### 22.6.4 `bool validateZeroLevelData()`
Finalizes the ZeroLevel EEPROM region and makes it valid (read-only).

Behavior:
- Computes Fletcher-16 over exactly `CAL_COUNT` float entries currently stored in EEPROM.
- Writes header fields in this order (atomic validity):
  1) count = `CAL_COUNT`
  2) crc
  3) signature (written last)

After success:
- `g_zl_valid = true`
- `g_zl_count = CAL_COUNT`

#### 22.6.5 `float getZeroLevel(uint32_t frequencyHz)`
Returns ZeroLevel value for the ZeroLevel bucket containing `frequencyHz` (bucket math is internal).

Rules:
- If ZeroLevel EEPROM region is invalid (`g_zl_valid==false`), return `0.0f`.
- If `frequencyHz` out of range, return `0.0f`.
- Reads the float entry for that bucket from EEPROM.
- If stored float is `0.0f`, return `0.0f`.

Implementation may use a **single-entry cache** to avoid EEPROM reads when the bucket does not change.

<details>
<summary><strong>Rev21 version of this section (preserved)</strong></summary>

Returns ZeroLevel value for the MHz bucket containing `frequencyHz`.

Rules:
- If ZeroLevel EEPROM region is invalid (`g_zl_valid==false`), return `0.0f`.
- If `frequencyHz` out of range, return `0.0f`.
- Reads the float entry for that MHz bucket from EEPROM.
- If stored float is `0.0f`, return `0.0f`.

Implementation may use a **single-entry cache** to avoid EEPROM reads when the MHz bucket does not change.

</details>

#### 22.6.6 Optional helper: `void zeroLevel_get_state(bool *valid, uint8_t *count)`
Returns the cached startup state:
- `*valid = g_zl_valid`
- `*count = g_zl_count`

---

## 23 Code bundle (ready to paste) — ZeroLevel EEPROM (end-of-EEPROM) + Fletcher-16 + minimal RAM
```cpp
#include <stdint.h>
#include <stdbool.h>

#include <avr/eeprom.h>
#include <avr/io.h>   // E2END

// =============================
// Calibration constants (SSOT)
// =============================
#define CAL_MHZ_MIN  1u
#define CAL_MHZ_MAX  60u
#define CAL_STEP_HZ  1000000UL

#define CAL_MIN_HZ   ((uint32_t)CAL_MHZ_MIN * 1000000UL)
#define CAL_MAX_HZ   ((uint32_t)CAL_MHZ_MAX * 1000000UL)
#define CAL_COUNT    (((CAL_MAX_HZ - CAL_MIN_HZ) / CAL_STEP_HZ) + 1u)

// =============================
// EEPROM region at end-of-EEPROM
// =============================
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

// Cached state
static bool    g_zl_inited = false;
static bool    g_zl_valid  = false;
static uint8_t g_zl_count  = 0;

// =============================
// Fletcher-16 over EEPROM bytes
// =============================
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
  return (int16_t)((frequencyHz - CAL_MIN_HZ) / CAL_STEP_HZ);  // 0..CAL_COUNT-1
}

// =============================
// Validation primitive (no cache updates)
// =============================
bool verifyZeroLevelData()
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

// Lazy init (called by public APIs)
static void zeroLevel_lazy_init()
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

// =============================
// Public API (SSOT)
// =============================

void zeroLevel_factory_reset()
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

bool validateZeroLevelData()
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

// =============================
// Calibration usage pattern (SSOT)
// =============================
//
// 1) zeroLevel_factory_reset();
// 2) for each calibration bucket covering 1.000–60.000 MHz:
//      - measure Data
//      - compute zl = sqrt(Data)*1000
//      - saveZeroLevel(freqHz, zl)
// 3) validateZeroLevelData();
//
// NOTE: Bucket selection and indexing are internal to the module via CAL_STEP_HZ.
//
```

---

**END OF SSOT**



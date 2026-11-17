// CRSminer - Copyright (c) 2026 HexOhm
// Released under the MIT License (full text: open http://crsminer-NAME.local/license, or see the
// block at the end of this comment). Third-party credits are listed on the same page.
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
//   and associated documentation files (the "Software"), to deal in the Software without
//   restriction, including without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
//   Software is furnished to do so, subject to the following conditions: the above copyright
//   notice and this permission notice shall be included in all copies or substantial portions of
//   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
//   PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
//   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
//
// CRSminer CYD - multi-core SHA-256d miner (Bitcoin, DigiByte-SHA256, BCH, ...) for the
// ESP32-2432S028R "Cheap Yellow Display" with live on-screen monitoring
// WiFiManager setup portal + web dashboard.
//
// Arduino IDE setup:
//   Board: "ESP32 Dev Module" (or your S3 board), CPU Frequency: 240 MHz
//   C3 / C6 / XIAO boards (software SHA, single core, ~10 kH/s): pick your board (e.g. "ESP32C3 Dev Module",
//   "XIAO_ESP32C3", "ESP32C6 Dev Module") and set "USB CDC On Boot: Enabled" to see the serial output.
//   Libraries (Library Manager): "WiFiManager" by tzapu, "ArduinoJson" v7.x
//   Folder and file name must both be "CRSminer_CYD".
//   CYD display: install the "LovyanGFX" library (by lovyan03) as well. Select board
//   "ESP32 Dev Module". IMPORTANT: Tools > Partition Scheme > "Huge APP (3MB No OTA/1MB SPIFFS)"
//   (or "No OTA (2MB APP/2MB SPIFFS)") - this sketch is larger than the default 1.28 MB slot and
//   would otherwise stop with "text section exceeds available space in board".
//   Tap the screen to switch between the status page and the info page.
//   Defaults suit the two-USB-port CYD (TPM408-2.8, ST7789, landscape). Single-USB-port CYD (ILI9341)?
//   Add "#define CYD_PANEL_ILI9341 1" above the UI section. Wrong colours? #define CYD_INVERT 1 or
//   #define CYD_RGB_SWAP 1. Image upside down? #define CYD_ROTATION 3.
//
// Hardware SHA-256 (classic ESP32 only): the SHA peripheral is driven by a hand-scheduled
// Xtensa assembly loop. On boot it is checked against known answers (incl. the Bitcoin
// genesis block); if a loop fails, the next, safer one is used, finally plain software.
// Every candidate the hardware reports is re-hashed in software before it is submitted.
// The assembly loops and the known-answer test are adapted from NerdMiner Modded by
// HenrysCat / NerdMiner V2 by BitMaker-hub / dwespl/nerdminer-axehub (MIT License,
// Copyright (c) 2023 Bitmaker). MIT notice: permission is granted to use, copy, modify and
// distribute this code provided the above copyright notice and this permission notice are
// kept; the software is provided "as is", without warranty of any kind.
// The status-LED pin is a setting (portal / dashboard, default GPIO 2, -1 = none).
// Chips older than revision 3 (silicon bug) use the software path; to force hardware SHA on
// such a chip anyway add "#define CRS_FORCE_HW_SHA 1" at the top of this file.
#pragma GCC optimize("O3,unroll-loops")

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define BLUE_LED_PIN -1  // GPIO2 is the display DC pin on the CYD: never touch it
// pins used by the CYD display, touch, backlight and SD card cannot be chosen as the LED pin
#define CRS_RESERVED_PIN(p) ((p)==2 || (p)==5 || (p)==12 || (p)==13 || (p)==14 || (p)==15 || (p)==18 || (p)==19 || (p)==21 || (p)==23 || (p)==25 || (p)==32 || (p)==33)
// ======== CYD display UI (inlined) ========
// CYD_UI.h - display, touch and LED UI for the ESP32-2432S028R "Cheap Yellow Display".
// Needs the "LovyanGFX" library (Library Manager, author lovyan03). The pin configuration
// for the CYD is built in, so no User_Setup.h editing is required.
//
// The defaults below match a two-USB-port CYD (panel marking TPM408-2.8, ST7789 controller):
// landscape, normal colours. If the colours or orientation look wrong on your board, define
// one of these BEFORE including this file (or edit the defaults below):
//   CYD_PANEL_ILI9341  single-USB-port CYD with an ILI9341 controller (instead of the ST7789 default)
//   CYD_INVERT 1       colours look like a photo negative
//   CYD_RGB_SWAP 1     red and blue are swapped
//   CYD_ROTATION 3     flip the landscape orientation (1 or 3)
//   CYD_MASCOT 0       character in the logo button: 0 = sailor, 1 = miner (default)
//   CYD_GRAPH_MS 125   graph: one new sample every this many milliseconds
//   CYD_GRAPH_PX 2     graph: pixels it moves per sample (speed = PX * 1000 / MS pixels per second)
#define LGFX_USE_V1
#include <Arduino.h>
#include <ctype.h>
#include <LovyanGFX.hpp>

#if !defined(CYD_PANEL_ST7789) && !defined(CYD_PANEL_ILI9341)
#define CYD_PANEL_ST7789 1
#endif
#ifndef CYD_ROTATION
#define CYD_ROTATION 1
#endif
#ifndef CYD_RGB_SWAP
#define CYD_RGB_SWAP 0
#endif
#ifndef CYD_INVERT
#define CYD_INVERT 0
#endif
#ifndef CYD_BACKLIGHT
#define CYD_BACKLIGHT 200  // 0..255
#endif
#ifndef CYD_MASCOT
#define CYD_MASCOT 1
#endif
#ifndef CYD_GRAPH_MS
#define CYD_GRAPH_MS 125
#endif
#ifndef CYD_GRAPH_PX
#define CYD_GRAPH_PX 2
#endif
#ifndef UI_HASH_DY
#define UI_HASH_DY 6   // pixels the big hashrate number sits lower (try 2..8)
#endif
#define CYD_LED_R 4        // on-board RGB LED, active LOW
#define CYD_LED_G 16
#define CYD_LED_B 17


// ------------------------------------------------------------ display config (CYD pinout)
class LGFX_CYD : public lgfx::LGFX_Device {
#ifdef CYD_PANEL_ST7789
  lgfx::Panel_ST7789 _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

 public:
  LGFX_CYD() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = (CYD_INVERT != 0);
      cfg.rgb_order = (CYD_RGB_SWAP != 0);
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 200;
      cfg.y_max = 3700;
      cfg.pin_int = 36;
      cfg.bus_shared = false;
      cfg.offset_rotation = 6;
      cfg.spi_host = SPI3_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX_CYD tft;
static lgfx::LGFX_Sprite mspr(&tft);  // 96x96 sprite for the flicker-free mascot
static bool uiSprOk = false;

// ------------------------------------------------------------ data the sketches fill in
struct UiSnap {
  char title[16];   // "CRSminer" / "CRSduco"
  char host[40];    // "crsminer-rig1.local"
  float hr;         // total H/s
  float cores[4];
  int nCores;
  uint32_t acc, rej;
  uint32_t up;      // seconds
  bool ok;          // connected / working
  int rssi;
  float temp;
  uint32_t heap;
  char ip[20], mac[20], pool[48], err[80];
  char extraL[16];  // label of one coin-specific value ("Best diff" / "Ping")
  char extraV[28];
};

// ------------------------------------------------------------ small helpers
static inline uint16_t UC(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
#define UC_BG UC(10, 12, 18)       // screen background
#define UC_BAR UC(20, 24, 34)      // header bar
#define UC_CARD UC(22, 27, 40)     // card / tile fill
#define UC_EDGE UC(46, 55, 80)     // card outline
#define UC_ACC UC(255, 155, 0)     // orange (hashrate, title)
#define UC_GOLD UC(255, 200, 70)
#define UC_CYAN UC(40, 200, 255)
#define UC_MAG UC(214, 100, 255)
#define UC_OK UC(62, 207, 142)
#define UC_BAD UC(255, 92, 92)
#define UC_MUT UC(139, 146, 165)
#define UC_FG UC(232, 234, 240)
#define UD_TL ((lgfx::textdatum_t)0)  // top-left
#define UD_TC ((lgfx::textdatum_t)1)  // top-centre
#define UD_TR ((lgfx::textdatum_t)2)  // top-right

enum { UI_SLEEP = 0, UI_WORK = 1, UI_CHEER = 2 };

static void ui_si(double v, char *o, size_t n) {
  static const char *u[] = {"", "K", "M", "G", "T", "P"};
  int i = 0;
  while (v >= 1000 && i < 5) { v /= 1000; i++; }
  snprintf(o, n, v < 10 ? "%.2f%s" : "%.1f%s", v, u[i]);
}

static void ui_rate(float hr, char *num, size_t n, const char **unit) {
  if (hr >= 1e6f) { snprintf(num, n, "%.2f", hr / 1e6f); *unit = "MH/s"; }
  else if (hr >= 1e3f) { snprintf(num, n, "%.1f", hr / 1e3f); *unit = "kH/s"; }
  else { snprintf(num, n, "%.0f", hr); *unit = "H/s"; }
}

static bool uiLedEnabled = true;  // the sketches clear this when the user turned the LED off
static void ui_led(bool r, bool g, bool b) {
  if (!uiLedEnabled) r = g = b = false;
  digitalWrite(CYD_LED_R, r ? LOW : HIGH);
  digitalWrite(CYD_LED_G, g ? LOW : HIGH);
  digitalWrite(CYD_LED_B, b ? LOW : HIGH);
}

// ------------------------------------------------------------ mascot (original character)
// A cheerful round-faced sailor with a white cup hat, drawn with plain shapes.
// Draws a 96x96 picture at (ox, oy) on any target (screen or sprite).
static void ui_mascot(lgfx::LovyanGFX *g, int ox, int oy, int mood, bool blink, int frame) {
  const uint16_t SEA = UC(22, 68, 118), SEA2 = UC(48, 110, 170), WHITE = 0xFFFF, NAVY = UC(18, 40, 90);
  const uint16_t RED = UC(210, 40, 50), SKIN = UC(250, 205, 160), SKIN2 = UC(232, 170, 125);
  const uint16_t DARK = UC(30, 24, 30), GOLD = UC(255, 196, 40), GOLD2 = UC(190, 130, 10);
  const uint16_t MOUTH = UC(140, 30, 40), CHEEK = UC(245, 140, 140), GREY = UC(205, 210, 222);
  const int cx = ox + 48;

  g->setClipRect(ox, oy, 96, 96);
  g->fillRect(ox, oy, 96, 96, UC_BG);  // so the rounded corners match the screen background
  g->fillRoundRect(ox, oy, 96, 96, 14, SEA);
  g->drawRoundRect(ox, oy, 96, 96, 14, SEA2);

  // shirt with navy stripes
  g->fillEllipse(cx, oy + 100, 40, 30, WHITE);
  for (int k = 0; k < 2; k++) {
    int y = oy + 82 + k * 9, dy = y - (oy + 100);
    int hw = (int)(40.0f * sqrtf(1.0f - (dy * dy) / 900.0f));
    g->fillRect(cx - hw + 3, y, 2 * hw - 6, 3, NAVY);
  }
  g->fillTriangle(cx - 16, oy + 70, cx + 16, oy + 70, cx, oy + 90, NAVY);   // collar
  g->fillTriangle(cx - 9, oy + 71, cx + 9, oy + 71, cx, oy + 85, RED);      // neckerchief
  g->fillCircle(cx, oy + 72, 4, RED);                                       // knot

  // head and ears
  g->fillCircle(cx - 25, oy + 52, 5, SKIN);
  g->fillCircle(cx + 25, oy + 52, 5, SKIN);
  g->fillCircle(cx, oy + 50, 25, SKIN);

  // cheeks and nose
  g->fillCircle(cx - 16, oy + 58, 3, CHEEK);
  g->fillCircle(cx + 16, oy + 58, 3, CHEEK);
  g->fillCircle(cx, oy + 55, 3, SKIN2);

  // eyes
  if (mood == UI_SLEEP || blink) {
    g->drawFastHLine(cx - 14, oy + 47, 9, DARK);
    g->drawFastHLine(cx - 14, oy + 48, 9, DARK);
    g->drawFastHLine(cx + 5, oy + 47, 9, DARK);
    g->drawFastHLine(cx + 5, oy + 48, 9, DARK);
  } else {
    int look = ((frame / 6) % 4 == 1) ? -2 : (((frame / 6) % 4 == 3) ? 2 : 0);
    g->fillEllipse(cx - 9, oy + 46, 5, 6, WHITE);
    g->fillEllipse(cx + 9, oy + 46, 5, 6, WHITE);
    g->fillCircle(cx - 9 + look, oy + 47, 2, DARK);
    g->fillCircle(cx + 9 + look, oy + 47, 2, DARK);
    g->drawLine(cx - 14, oy + 38, cx - 5, oy + 39, DARK);
    g->drawLine(cx + 5, oy + 39, cx + 14, oy + 38, DARK);
  }

  // mouth
  if (mood == UI_CHEER) {
    g->fillEllipse(cx, oy + 62, 8, 6, MOUTH);
    g->fillEllipse(cx, oy + 65, 4, 2, CHEEK);
  } else if (mood == UI_WORK) {
    for (int dx = -9; dx <= 9; dx++) g->fillCircle(cx + dx, oy + 64 - (dx * dx) / 14, 1, MOUTH);
  } else {
    g->drawFastHLine(cx - 6, oy + 63, 12, MOUTH);
  }

  // sailor cup hat with a little gold coin badge
  g->fillRoundRect(cx - 22, oy + 11, 44, 22, 9, WHITE);
  g->fillRect(cx - 22, oy + 27, 44, 4, NAVY);
  g->fillRoundRect(cx - 27, oy + 30, 54, 9, 4, WHITE);
  g->drawRoundRect(cx - 27, oy + 30, 54, 9, 4, GREY);
  g->fillCircle(cx, oy + 19, 5, GOLD);
  g->drawCircle(cx, oy + 19, 5, GOLD2);
  g->fillCircle(cx, oy + 19, 2, GOLD2);

  if (mood == UI_CHEER) {  // sparkles
    int s = frame % 6;
    int sx[2] = {ox + 13, ox + 82}, sy[2] = {oy + 16 + s, oy + 30 - s};
    for (int i = 0; i < 2; i++) {
      g->drawFastHLine(sx[i] - 3, sy[i], 7, GOLD);
      g->drawFastVLine(sx[i], sy[i] - 3, 7, GOLD);
    }
  } else if (mood == UI_SLEEP) {  // snoring
    g->setFont(&lgfx::fonts::Font2);
    g->setTextColor(WHITE);
    g->setTextDatum(UD_TL);
    g->drawString("z", ox + 76, oy + 12 + (frame / 4) % 3);
    g->drawString("Z", ox + 82, oy + 4 + (frame / 4) % 3);
  }
  g->clearClipRect();
}

// ------------------------------------------------------------ mascot 2: cartoon miner (original character)
// A clean-shaven grinning miner in a yellow hard hat with a headlamp, orange work shirt and blue dungarees, swinging a
// pickaxe next to a crystal. Same 96x96 size, moods and animation hooks as the sailor above.
// Pick the character with CYD_MASCOT: 0 = sailor, 1 = miner (default).
static void ui_miner(lgfx::LovyanGFX *g, int ox, int oy, int mood, bool blink, int frame) {
  const uint16_t CAVE = UC(30, 38, 58), CAVE2 = UC(62, 78, 112), ROCK = UC(44, 54, 80);
  const uint16_t ORANGE = UC(238, 122, 28), BLUE = UC(52, 92, 168), BLUE2 = UC(34, 64, 128);
  const uint16_t SKIN = UC(250, 205, 160), SKIN2 = UC(232, 170, 125), CHEEK = UC(245, 140, 140);
  const uint16_t NOSE = UC(238, 150, 120), SOOT = UC(108, 92, 84), BROWN = UC(120, 72, 34);
  const uint16_t DARK = UC(30, 24, 30), WHITE = 0xFFFF, MOUTH = UC(140, 30, 40);
  const uint16_t YEL = UC(255, 200, 0), YEL2 = UC(214, 150, 0), GOLD = UC(255, 196, 40);
  const uint16_t GEM = UC(70, 220, 255), GEM2 = UC(24, 140, 200), GEMD = UC(70, 110, 130), GEMD2 = UC(44, 74, 92);
  const uint16_t WOOD = UC(160, 100, 48), STEEL = UC(196, 204, 220), STEEL2 = UC(120, 130, 150);
  const int cx = ox + 48;

  g->setClipRect(ox, oy, 96, 96);
  g->fillRect(ox, oy, 96, 96, UC_BG);  // so the rounded corners match the screen background
  g->fillRoundRect(ox, oy, 96, 96, 14, CAVE);
  g->drawRoundRect(ox, oy, 96, 96, 14, CAVE2);

  // cave rocks and a crystal (dull while asleep)
  g->fillEllipse(ox + 4, oy + 8, 16, 12, ROCK);
  g->fillEllipse(ox + 94, oy + 14, 14, 18, ROCK);
  g->fillEllipse(ox + 8, oy + 94, 20, 12, ROCK);
  const bool dull = (mood == UI_SLEEP);
  g->fillTriangle(ox + 4, oy + 66, ox + 20, oy + 66, ox + 12, oy + 56, dull ? GEMD : GEM);
  g->fillTriangle(ox + 4, oy + 66, ox + 20, oy + 66, ox + 12, oy + 80, dull ? GEMD2 : GEM2);
  if (!dull) {
    g->drawLine(ox + 12, oy + 56, ox + 9, oy + 66, WHITE);
    g->drawLine(ox + 9, oy + 66, ox + 12, oy + 74, UC(190, 240, 255));
  }

  // work shirt and dungarees
  g->fillEllipse(cx, oy + 100, 40, 30, ORANGE);
  g->fillRoundRect(cx - 17, oy + 76, 34, 22, 4, BLUE);
  g->fillRect(cx - 17, oy + 69, 6, 9, BLUE);
  g->fillRect(cx + 11, oy + 69, 6, 9, BLUE);
  g->fillRect(cx - 7, oy + 86, 14, 9, BLUE2);  // pocket
  g->fillCircle(cx - 14, oy + 80, 2, GOLD);
  g->fillCircle(cx + 14, oy + 80, 2, GOLD);

  // head, ears, cheeks, nose, soot smudge
  g->fillCircle(cx - 25, oy + 52, 5, SKIN);
  g->fillCircle(cx + 25, oy + 52, 5, SKIN);
  g->fillCircle(cx, oy + 50, 25, SKIN);
  g->fillCircle(cx - 16, oy + 58, 3, CHEEK);
  g->fillCircle(cx + 16, oy + 58, 3, CHEEK);
  g->fillCircle(cx, oy + 55, 4, NOSE);

  // eyes and bushy brows
  if (mood == UI_SLEEP || blink) {
    g->drawFastHLine(cx - 14, oy + 47, 9, DARK);
    g->drawFastHLine(cx - 14, oy + 48, 9, DARK);
    g->drawFastHLine(cx + 5, oy + 47, 9, DARK);
    g->drawFastHLine(cx + 5, oy + 48, 9, DARK);
  } else {
    int look = ((frame / 6) % 4 == 1) ? -2 : (((frame / 6) % 4 == 3) ? 2 : 0);
    g->fillEllipse(cx - 9, oy + 46, 5, 6, WHITE);
    g->fillEllipse(cx + 9, oy + 46, 5, 6, WHITE);
    g->fillCircle(cx - 9 + look, oy + 47, 2, DARK);
    g->fillCircle(cx + 9 + look, oy + 47, 2, DARK);
    g->drawLine(cx - 14, oy + 38, cx - 5, oy + 40, BROWN);
    g->drawLine(cx - 14, oy + 39, cx - 5, oy + 41, BROWN);
    g->drawLine(cx + 5, oy + 40, cx + 14, oy + 38, BROWN);
    g->drawLine(cx + 5, oy + 41, cx + 14, oy + 39, BROWN);
  }

  // mouth
  if (mood == UI_CHEER) {
    g->fillEllipse(cx, oy + 68, 6, 4, MOUTH);
  } else if (mood == UI_WORK) {
    for (int dx = -7; dx <= 7; dx++) g->fillCircle(cx + dx, oy + 68 - (dx * dx) / 16, 1, MOUTH);
  } else {
    g->drawFastHLine(cx - 5, oy + 67, 10, MOUTH);
  }

  // hard hat with a headlamp that flickers while working
  g->fillRoundRect(cx - 24, oy + 11, 48, 22, 11, YEL);
  g->fillRect(cx - 3, oy + 11, 6, 20, YEL2);
  g->fillRoundRect(cx - 30, oy + 29, 60, 8, 4, YEL);
  g->drawFastHLine(cx - 28, oy + 36, 56, YEL2);
  uint16_t lamp = mood == UI_SLEEP ? UC(110, 110, 120)
                  : (mood == UI_CHEER ? WHITE : (((frame / 3) % 4 == 0) ? UC(255, 245, 170) : UC(255, 226, 90)));
  g->fillCircle(cx, oy + 21, 8, UC(74, 80, 96));
  g->fillCircle(cx, oy + 21, 5, lamp);
  g->drawCircle(cx, oy + 21, 8, STEEL2);
  if (mood != UI_SLEEP) {  // light rays
    g->drawLine(cx, oy + 10, cx, oy + 3, lamp);
    g->drawLine(cx - 9, oy + 10, cx - 15, oy + 4, lamp);
    g->drawLine(cx + 9, oy + 10, cx + 15, oy + 4, lamp);
  }

  // pickaxe: swings while working, raised when cheering, resting when asleep
  static const int8_t SW[4][2] = {{92, 38}, {94, 48}, {88, 64}, {93, 48}};
  int hxo = 86, hyo = 64;
  if (mood == UI_WORK) { hxo = SW[frame & 3][0]; hyo = SW[frame & 3][1]; }
  else if (mood == UI_CHEER) { hxo = 92, hyo = 30; }
  const float bx = ox + 78, by = oy + 97, hx = ox + hxo, hy = oy + hyo;
  float ux = hx - bx, uy = hy - by;
  const float len = sqrtf(ux * ux + uy * uy);
  ux /= len; uy /= len;
  const float px = -uy, py = ux;  // across the handle
  for (int d = -1; d <= 1; d++) g->drawLine((int)bx + d, (int)by, (int)hx + d, (int)hy, WOOD);
  const int fx = (int)(bx + ux * len * 0.38f), fy = (int)(by + uy * len * 0.38f);
  g->fillCircle(fx, fy, 5, SKIN);  // hand
  g->drawCircle(fx, fy, 5, SKIN2);
  for (int t = -1; t <= 2; t++)  // steel head
    g->drawLine((int)(hx - px * 11 + ux * t), (int)(hy - py * 11 + uy * t), (int)(hx + px * 11 + ux * t), (int)(hy + py * 11 + uy * t), t == 2 ? STEEL2 : STEEL);
  for (int e = -1; e <= 1; e += 2)  // pointed tips
    g->fillTriangle((int)(hx + e * px * 15), (int)(hy + e * py * 15),
                    (int)(hx + e * px * 10 + ux * 3), (int)(hy + e * py * 10 + uy * 3),
                    (int)(hx + e * px * 10 - ux * 2), (int)(hy + e * py * 10 - uy * 2), STEEL);
  if (mood == UI_WORK && (frame & 3) == 2) {  // sparks where the pick hits
    g->fillCircle(ox + 84, oy + 74, 1, GOLD);
    g->fillCircle(ox + 92, oy + 72, 1, WHITE);
    g->fillCircle(ox + 80, oy + 72, 1, GOLD);
  }

  // extras
  if (mood == UI_CHEER) {  // sparkles round the crystal
    int s = frame % 6;
    int sx[2] = {ox + 9, ox + 24}, sy[2] = {oy + 50 + s / 2, oy + 74 - s / 2};
    for (int i = 0; i < 2; i++) {
      g->drawFastHLine(sx[i] - 3, sy[i], 7, i ? GOLD : WHITE);
      g->drawFastVLine(sx[i], sy[i] - 3, 7, i ? GOLD : WHITE);
    }
  } else if (mood == UI_WORK && (frame % 8) == 0) {  // crystal twinkle
    g->drawFastHLine(ox + 9, oy + 58, 7, WHITE);
    g->drawFastVLine(ox + 12, oy + 55, 7, WHITE);
  } else if (mood == UI_SLEEP) {  // snoring
    g->setFont(&lgfx::fonts::Font2);
    g->setTextColor(WHITE);
    g->setTextDatum(UD_TL);
    g->drawString("z", ox + 76, oy + 12 + (frame / 4) % 3);
    g->drawString("Z", ox + 82, oy + 4 + (frame / 4) % 3);
  }
  g->clearClipRect();
}

// Draws whichever character CYD_MASCOT selects.
static void uiMascot(lgfx::LovyanGFX *g, int ox, int oy, int mood, bool blink, int frame) {
#if CYD_MASCOT == 1
  ui_miner(g, ox, oy, mood, blink, frame);
#else
  ui_mascot(g, ox, oy, mood, blink, frame);
#endif
}

// ------------------------------------------------------------ brand logo (header, upper left)
// 36x24 px, RGB565, pre-blended onto the header bar colour. Pixels equal to UC_BAR are skipped when drawing.
#define UI_LOGO_W 36
#define UI_LOGO_H 24
static const uint16_t UI_LOGO[UI_LOGO_W * UI_LOGO_H] = {
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x08E9, 0x26DF, 0x2EDF, 0x10CA, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x03F9, 0x155E, 0x1D5E, 0x0419, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x094A, 0x163F, 0x0E1F, 0x094A, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x1928, 0x10C4, 0x041A, 0x045B, 0x10C4, 0x1148, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x09AE, 0x3EBF, 0x1AF6, 0x0BD8, 0x0BFA, 0x1AB4, 0x3EDF, 0x09CF, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x4CDA, 0x275F, 0x02B1, 0x0337, 0x0377, 0x0270, 0x1F3F, 0x44FA, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x198D, 0xA5FC, 0x7332, 0x008E, 0x0850, 0x161F, 0x15DF, 0x104F, 0x00AE, 0x6B11, 0xA5DD, 0x21EE, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x5BD6, 0xA538, 0x4A50, 0x22D9, 0x4E5F, 0x3FFF, 0xC7FF, 0xBFFF, 0x2FDF, 0x3DFF, 0x22FA, 0x4A71, 0x9CFA, 0x5C17, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x5BD4, 0x9C35, 0x29D1, 0x255F, 0x059F, 0x065F, 0x07FF, 0x7FFF, 0x6FFF, 0x07FF, 0x04BF, 0x049F, 0x159F, 0x29F3, 0x8C16, 0x5BD5, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x2313, 0x3EFC, 0x10AB, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x18A9, 0x53D9, 0x07FF, 0x04DF, 0x035F, 0x053F, 0x05DF, 0x3FFF, 0x2FFF, 0x055F, 0x047F, 0x03BF, 0x059F, 0x17FF, 0x5C39, 0x1888, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10CA, 0x3EDC, 0x22F4, 0x10C4,
  0x090A, 0x1EFF, 0x1C96, 0x269F, 0x2479, 0x2D78, 0x1990, 0x10C4, 0x10C4, 0x0885, 0x42F9, 0x97FF, 0x07FF, 0x079F, 0x049F, 0x01F7, 0x021F, 0x05FF, 0x05FF, 0x023E, 0x01F8, 0x04FF, 0x07FF, 0x1FFF, 0xAFFF, 0x42D8, 0x0865, 0x10C4, 0x10C4, 0x196F, 0x2D58, 0x2459, 0x1E5F, 0x1C56, 0x26DF, 0x08E9,
  0x08C6, 0x05BF, 0x1D5F, 0x051F, 0x045B, 0x051E, 0x2FFF, 0x2193, 0x10C4, 0x1072, 0x7FFF, 0x57FF, 0x6FFF, 0x46BE, 0x0010, 0x008E, 0x088B, 0x088D, 0x088D, 0x086A, 0x006C, 0x0011, 0x56BF, 0x87FF, 0x5FFF, 0x87FF, 0x1071, 0x10C4, 0x21D2, 0x27FF, 0x04FD, 0x045A, 0x053F, 0x157F, 0x059F, 0x08A6,
  0x10C4, 0x08E9, 0x0292, 0x1083, 0x10C4, 0x10A3, 0x0379, 0x27FF, 0x18CF, 0x3C3D, 0x07FF, 0x07FF, 0x77FF, 0x108F, 0x000A, 0x10A4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x0009, 0x0890, 0x77FF, 0x07FF, 0x07FF, 0x3C3C, 0x18AF, 0x27FF, 0x0337, 0x10A3, 0x10C4, 0x10A3, 0x0272, 0x08C8, 0x10C4,
  0x10C4, 0x10C4, 0x2A6F, 0x118C, 0x10C4, 0x10C4, 0x10C4, 0x0292, 0x169F, 0x265F, 0x06BF, 0x07FF, 0x259E, 0x0005, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x0006, 0x359F, 0x07FF, 0x071F, 0x267F, 0x167F, 0x0292, 0x10C4, 0x10C4, 0x10C4, 0x118C, 0x2A4F, 0x10C4, 0x10C4,
  0x10C4, 0x13F9, 0x36FF, 0x1DBF, 0x249A, 0x24F8, 0x2D38, 0x104A, 0x0005, 0x0E3F, 0x041F, 0x067F, 0x139A, 0x0021, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x0021, 0x1BBB, 0x073F, 0x049F, 0x0E5F, 0x0005, 0x104A, 0x2D18, 0x24F8, 0x249A, 0x1D9F, 0x2F1F, 0x0BB9, 0x10C4,
  0x10C4, 0x0376, 0x0E3F, 0x059F, 0x04BD, 0x04DE, 0x04FC, 0x165F, 0x31D0, 0x04FF, 0x029F, 0x04DF, 0x0B9F, 0x0863, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x0863, 0x13DF, 0x053F, 0x02BF, 0x04FF, 0x31F0, 0x161F, 0x04FC, 0x04BF, 0x04BD, 0x057F, 0x0E3F, 0x0355, 0x10C4,
  0x10C4, 0x1084, 0x0233, 0x08A7, 0x10C4, 0x10C4, 0x0883, 0x041B, 0x14FF, 0x0CDF, 0x023F, 0x03BF, 0x15BF, 0x084C, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x084D, 0x15DF, 0x03DF, 0x023F, 0x0C9F, 0x0CDF, 0x041A, 0x0883, 0x10C4, 0x10C4, 0x08C8, 0x0212, 0x1084, 0x10C4,
  0x12D6, 0x3FDF, 0x2378, 0x18E9, 0x19CB, 0x196D, 0x271F, 0x151D, 0x61AA, 0x151F, 0x02FF, 0x023F, 0x073F, 0x0AFD, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x0B3E, 0x071F, 0x01FF, 0x02FF, 0x14FF, 0x6189, 0x155C, 0x271F, 0x194D, 0x19CB, 0x18E8, 0x2377, 0x3F9F, 0x1A95,
  0x061F, 0x1415, 0x0F7F, 0x0E5F, 0x0E5F, 0x16DF, 0x053F, 0x0862, 0x6AEC, 0x32D8, 0x053F, 0x029F, 0x04DF, 0x2FFF, 0x11B9, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x11B8, 0x27FF, 0x043F, 0x029F, 0x055F, 0x2AB8, 0x72EB, 0x0862, 0x053F, 0x16DF, 0x0E5F, 0x0E5F, 0x0F5F, 0x1415, 0x05DF,
  0x0212, 0x165F, 0x035A, 0x0865, 0x08C8, 0x0888, 0x1083, 0x10C4, 0x1106, 0x72CC, 0x00AE, 0x033F, 0x03FF, 0x07FF, 0x47FF, 0x249F, 0x08A9, 0x10C4, 0x10C4, 0x08A9, 0x24BF, 0x37FF, 0x07FF, 0x037F, 0x035F, 0x008C, 0x72CC, 0x1906, 0x10C4, 0x1083, 0x0887, 0x08C8, 0x0865, 0x039B, 0x0E3F, 0x01D1,
  0x10C4, 0x0885, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x08C7, 0x1C9F, 0x4CBF, 0x3AF5, 0x3B33, 0x5C33, 0x16BF, 0x07FF, 0x0EDF, 0x0045, 0x10C4, 0x10C4, 0x0046, 0x16DF, 0x07FF, 0x06BF, 0x5C33, 0x3B33, 0x3AF5, 0x4CDF, 0x1C7F, 0x08C7, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x0885, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x0882, 0x23BE, 0x031F, 0x049F, 0x07FF, 0x1FFF, 0x07FF, 0x051F, 0x041F, 0x0841, 0x10C4, 0x10C4, 0x0842, 0x041F, 0x051F, 0x07FF, 0x1FFF, 0x07FF, 0x045F, 0x02DF, 0x239E, 0x0862, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x0884, 0x135D, 0x031F, 0x051F, 0x2FFF, 0x57FF, 0x067F, 0x047F, 0x13DF, 0x0842, 0x10C4, 0x10C4, 0x0842, 0x13DF, 0x04BF, 0x06DF, 0x67FF, 0x27FF, 0x04BF, 0x031F, 0x133B, 0x0863, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
  0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x08E8, 0x01D7, 0x0111, 0x019F, 0x02FF, 0x01FF, 0x00BB, 0x00CF, 0x00AD, 0x0887, 0x10C4, 0x10C4, 0x0887, 0x00AE, 0x00D1, 0x00DE, 0x027F, 0x02DF, 0x017E, 0x0111, 0x0196, 0x08C8, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4, 0x10C4,
};

static void uiDrawLogo(int x, int y) {
  const uint16_t bar = UC_BAR;
  for (int j = 0; j < UI_LOGO_H; j++)
    for (int i = 0; i < UI_LOGO_W; i++) {
      uint16_t c = UI_LOGO[j * UI_LOGO_W + i];
      if (c != bar) tft.drawPixel(x + i, y + j, c);
    }
}


// ------------------------------------------------------------ start-up wordmark (HexOhm)
#define WM_W 220
#define WM_H 73
static const char WM_B64[] PROGMEM = "YghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCKQIKQnOAVMC1wLXApUCMgLPAUoBxQhjCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCIMI6ACwATcCmgL8Ap4DPwTfBN8EXwSeA9wCWQLVAZEBTQHIAGMAYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYgjnAG8BUQG2AboC3CMdVT523pZer563nr9+t/6mfpbedVxM2CK0Ac4AqgCoAIUAYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIxwAtARAB1AGZO72NHr++x97H3r/ev96/vs++z76vvqe+p76vvrd/v96+Go3UOu0AJwBmAGQAQghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCIUACwHNAPMJm3Q/r9+nXnfeRp42vjb+Rl5Xnm++l56fnndeX95Gfi4+Hj4enjY/Z7+nvba2W8sABQCFAGMAYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghjCGIIYwhjCGMIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghjCIMIgwiDCIMIgwhjCGMIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCOgAygAwAVlkX7d/j14ungWeBd4VHi6ePt5OPl+ed76Xnp9+dz5n3k5+Pj4u3hVeBR4FPgVeLr+X3bYTS0gARABkAEIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghjAKgAqQDrAC0BcAHTARUCNwI2AtQBkAELAWIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCOoALgGRAfUBeALcAhwD/AJ5AvQBcAErAWIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghjAOkAqgB0Kn2un5feJd4E3gQ+BZ4F/g0eLn4+/k4+X35vvpeen353/laeRl4uHha+BT4FHgW+BF4E3gReLt+nep3sCAQAZABCCGIIYghiCGIIZADqAE4BsQHzAVUCdwJVAvIBbAFjCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGUACBEMKk8qsjI2M3krGyy8LP0s+zOxAckAhgBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCKcACQFPKhUruiu9LJ0t3S09LVwsmTPwCckApwBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghjAMgAzAD4W5/H/07+BJ4EvgT+BD4FfgXeBf4dPja+Rj5Xnm+el56ffnf+Vp4+XibeBX4F/gSeBL4EfgR+BJ4EPgVfb9628iklAGQAQghiCGIIZADpCEwR0QmXClwLPgx+DHwL2QqzAW0BYwBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGQAAgANW9//fe+e9573nve+9773vvfdbVMCZQCGAEIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIRQBoIX3vnvee97733vfe977v3v+dtrUKZQCFAEIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghjAKcA7QBbdd+vPhYeBT4F/gS+BN4E/gReBb4F/hVeLp4+Hld+b56Xnpd+d/5Wnj4+Ht4NXgUeBb4EfgReBJ4E/gQeBT4F3kZ/v5QyJgBkAEIIQggjAOsp/d4d3z7fXt9+357fXt8+3zVshwDIAGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYwAAAC1jes52rXetl62XrZetd6W71hMj7wFDAEQAQghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIQggjAGch297Yvfi9Gb46vhq++bW71rZ80AGFAGQAQgBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiAIYA7QC8fd+nPhaeDZ4NfgX+BP4EvgS+BB4FngXeDT4WfiYeR55vvpe+l35n3kY+Lv4dvgU+Bd4EngR+BL4E3gQ+BZ4F3h2+Hf5en9cyKiUAQwBDACIAxwBXlfveGb5azlrGOsY6xprOvvfpOSMAhgBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghjAAAALWN6zlWldq1WpTalVqU2pbvWzzKLASIAIwBCCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghCCAIARim71re1+L34vdi12LW3tZrWtYSMAYQAQwBCAGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghCCIUAzABbhd+vfi4eLv4d/hWeBV4FPgW+BJ4E3gT+BN4F3iY+X36PXqdev36/Xqdelz5fviafBd4E3gS+BL4EHgVeBZ4F3hUeJl4+Pj5ehx7PbhFEAEMAAQDnAOsZOcbYtfi92LXYtdi12LW71g1jAQCGAEIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCEIAAAAtY7vWdq23tZatdq2WrXatutaNOkkBAQAiAEIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCEIIAgBGKbvW17XYvfi9+L34vbe1u9a0hGsBYwAjAEIAYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCIQAiQB5dN/H/k6+Rp4+fjYeHt4FngU+Bf4EvgQfBX4eWz4ZVRxE/iN+FH4FfgV+FP0zW1Q5ZX0+/w1+BL4EHgVeBX4FvhUeLj42nk7eZt5e378crqsAQwABAEEApgCTjNvet7W3tde12LXXtVnGcowAAGQAYwBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIQgAAAC1ju9Z2rde12L3Xtdi1t7W71o0ySQEBACIAQghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIQggCAEUp2974vRi+GcY5xhnG+L373rR8rAFjACIAQgBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGMARgD1Ot/nfnc+Zx5X3k6ePn42/hW+BV4F3gTfDTw2siuwCfkBfgP+BF4GXhdeF14GvgT9ApUB7xm1NH4uPwX+BJ4F3hUeLl4+3lb+Xh5vPn9ed9/PF1QmAEIAAABkAIxC2963tdi9+L3Yvdi9OcY1pQAAQwBjAGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghCAAAALWMc5/i9GMYYvhi+WcYZxtvejiJqAQEAIgBCAGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghCCAIARiH83lnOWs56znrOmtZaxjzntYTNAWMAIgBCAGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCEIAZQAtCd62vpd+d353Pl/+Vv5Wnj4eJv4VfgV/JhY9SxExAbwCvgN7BHwE/gReFT4VvgScBDsEvgM4Au0AbiJbRj8Ovg0eJl4+fkYeX15vXnd+f15vfoc+t00JIwAAAEIAqRm61vi9+L0ZxjnGGL56znatAQAiAEMAYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCEIAAAAsY37vms6a1nrOes56znrOHOePIowBIgAjAEIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCEIIAwBGIRznu9bb3tvW29b73tvWnffWhO8BgwACAEIIYghiCGIIYghiCGIIgwiDCIMIgwiDCKQIgwiDCIMIgwiDCIMIYghiCGIIYghiCGMIgwiDCIMIgwiDCIMIgwiDCGIIYghiCGIIYghiCGIIYghiCIMIgwiDCIMIgwhjCGMIYwhiCGIIZABHAHl037deZ55/nnd+d15vPmfeTp5GPi7/LlM0iADVAboCEwJtAW8B1QEYAnoCWQL3AXMBLQGtAZYC2wJQAUsJGja/Nj42nk7eVh5nXneef353Xm8+Z9+ndkwEACEAIQAGCVnOmtZ6znrOWs5ZxtvWt7UhAAEAQwBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYwilCMUIxQjmCOYI5ggHCSgJSAlpCYkJiQlpCScJBwkHCeYIxQjFCMUIpQilCKQIhAiECGMIYghiCGIIYghiCGIIYghiCGIIQgAAAAxjvvfb3tve297b3vze+94857AijQFDAEQAQghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYggkAEYhPOf83hzf/N4c5xznHN++/9aEEAKDAAEAQghiCGIIhAjoAOsADQFxAfUBWQLbAl4DvgPfA78DPQO7AjcC0wGRAU0BpQjIAOkAygDtAFEBtAEXAnkCmwJ7AjgCEwJiCGIIYghiCGIIYghiCAkB0gHVARcC9gGSAQ4BywCJACYAJQBlANERPsdeb15nfm9+b553fn9ebz5n3k5fV9U8iQDUAdMBCgHJAOoALAFOAZABsgGxAW8BTQHrAKgApwBtAXgCUAFqCbxO/1b+Xj5vfnd+f353Xm8+Zz5nXnfdhukAAgAAAIQAOcYc55vWu9a71rvWHN/XvSIAIQDHAC0BbwHzAXcC2gIcAzwD2gJ4AhQC0QFuASoBpQBiCGIIYghiCGIIYghiCGIIYghiCMcALAFPAXIBtQH3AVkCmwLeAl8D3wNfBL8E/wTfBH8EHwSfAz8D3QJZAvYBtAGSAVABDgHLAIkAiACIAMcAgwhiCGIIYghiCGIIYghCAAAADGPf/xznPOc85z3nPec9513v0iLPAWQAZQBCCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCEUARyE9513nXedd513nXe9d59//1YTvAWMAAQBCAGMAZQAEAKgADiIzQ9dTOlScVB1VvVU9Vn1WHVZ9Vf1Ue1SXS3EiCwFmAGUAhgDPQnNTtlMaVHxU/VRdVX1V2yuSASsBYghiCGIIYghiCIQATQHRAVxMvVR8VPhTlVNzUzBTL1vqOSUAV1Tfl95OPl8+X15vfm+ed353Xm+fb/lVygDUAZABhgCnAMgA6QDJAKYAhQCEAIUApgDIAMkAyACnAGUA6QBWAg0BjRK/fz5vfn9+f15vPmc+Zx5n/l6+Tr+P8RoDAAAAgwBaxn3v29783hznHN9d79e9IgADAMkAzhHUKnkrPSzeLF4tPi1dLLsrNyu0IvARywCIAOgAhABiCGIIYghiCGIIYgilAOkAiAAsAdMyGFzadBx1PHVddZ113XUddp12/Xb9dt12fW4ddr1tfW08bft02nS5dJh0d3RVdBR00mswU8spZQAEAKYAYghiCGIIYghiCGMAAAAMY9//Pedd713vXedd733vfe80IzMChgCGAEIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIZgCJIV3vfu99733vfe9+733v3//VfM0BYgAAAEMAAgDGENF7Ws6e99//3//f/97/3v/e/97/vv++/77/vv++/573fu9axg9bAgABAA5b3/+e9773vv+9/533ffde53QaDAGmAGIIYghiCGIICgHLAPl83v+d9773vve+/7733/9d56gxZwB8fh9Xvkb+Vh5XPl9eZ15vfn+ej56X7RFRAXABhQCmAKcAhQBiCGIIYghiCGIIYghiCGIIYgiFAIYAhgBEAMcA8wGpAFdd349+f15vPmceX/5e3lbeTp4+f3f3VAQAAACEAHrGnvdd713vXe895773+L0AAGxC2bVd5773vve+97733vfe9773vve+9573Hd+5re9SZQBlAIUAYghiCGIIYgiFAGUAhgCya7zWvvfe/97/vv+9/73/vf+9/53/nf99/33/ff+d/53/nf+d/73/vf++/77/vvee9573vve+/773Xe8YxoxSAgBkAGMAYghiCGIIYwAAAAxj3/9d713vXedd513vfe9971cjtwLIAOkA6QDoAAkBKgFLAWwBjQGuAa4BrgGNAWwBSwEJAcYA6ADIAMshXe+e9573fu997573fe/f/7R8agFCACEAAAApQvze3//f/573fu9d713vXecd5xzfHN/83tze29a71nrOes6azvvevvf0nCEAAAASfPzeGb4Zxhm+Ocb5vfvmPLYNAQoBYwhiCGIIxwBmABFDvvd6zjrGOr46xjnGWcZ6zukpAADsGX+Pfj6eRr5GvkbeTv5WPmc+Z9+f9mSrAJABZQCGAKcAYgBiCGIIYghiCGIIYghiCGIIYghiCGIIQgBlAGUAIwAKAS4BLBq+nz5n/l7+Vr5OnkaePp4+Xjb/TjtmZgAAAGMAWsbf/33vfe99713v3/fXva9z3//e/33vXecc5xznHecc5xzfHN/83tze/N4c53333//YteYYIwCEAGIIYghkAEQAxhD5vd//POe71pvWm86bzpvOm86bzpvWm856zlrGWsZaxlrGes5azlrGWsZaxjnGGb74vfi92L34vdi917XYvZrWnvf0nEMAZABiCGIIYghjAAAALWPf/z3nXec95z3nXed9733vWhP1AYoALgFQAZIB9gE4AnsCHQO/A18EXwTfAz4DuwI4AtUBcgFwAQ0BrBFd733vfe99733vfe9d79//k4TnAEEAAABqSr733/99733vnvee933vXe9d5z3nHOcc5/ze/N7b3rvWu9a71prWes5Zzp73kowAAIQI2LXb3hi+GcY5xhm++L1973Z8aADHAGIIhABFAIohXee71jnGWsY5xlnGWcbb3i5jAQABAJI7f2/eHX4+fjZ+Np4+3k7eTv5Wfm8OCg8BqABkAIYAQghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCEIIZABkAEMATAHqAHp2P1+eTp5Ofj5ePl42XjY+Ll4u3WYqAQAAIQBaxt//fe99733vXe9d7z3nvvdd7z3nPec95xznHOcc5xzn/N783tve297b3tveu9aa1p73utbFEEMAQgBCAGMAQwD4vb73Wc56znrOes56zprWms6a1nrOes56zlnOWcZZxjnGOcY5xjnGOcZZxlnGOcYYxvi9+L0Yvvi92L3Yvde9l612rV3vNaUBAGQAQghiCGMAAAAtY9//HOf83hzfHOcc5zznXud5ZNVjN2RYZFlkmmS7XP1cPl3eXV5e3l6+Zn5e/l1eXd1cu2R6XJlkeWR2dF3vXe895z3nXedd7z3n//9zfIUAIAAEGV3nvvd97573nvee933vXe9d7z3nHOcc3/ze3N671rvWu9a71rvWu9aa1prOes5974YxAADJKdveOcYYxhi+GcYZvjnGnvduOkUAxwBlAIUIWsZd7znGWs56zlrOOcbb3nKMQwAhAAMAFl3fRp4VHiYeHh4mPi5eNn4+/046VusADAFDAGUAQghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghCCEQAIgDIAKkA1UxfX14+XjYeJh4mHib+Hf4d3h0eV8wJAQAhADnGvvdd713vXe9d5xznXec85zznPec85zznHOcc5/ze297b3tve+97b3tveu9a71rvWes6+9zWlAABCAGMAAAARfN//Wc5ZzlnOWc5ZzlnOWc5ZxlnOWc45xjnGWcY5xjnGOcYYxhi+GMYYxhjGGL4Yxvi9173Yvde917XXtbe1t7W3tbe1Vq085+xaAQBjAGIIYwAAAC1jnvfb3vve297b3vve+94955333v/e/97/3v/e/97/3v/e/97/vv++/77/vv++/97/3v/e/97/3v///9//Xe8c5xznHOcc5xzn/N6+/3N8IgAAALOU3/9d733vfu99733vXe+e7573nfd9933vXO885zznPOcc57vWu9bb1rvWmtZ6zl3nrnMAACQAb2sc5/i9WcZZzlrOOcbb3rvW5wiFAAMA9Zy+93rOmtaa1nrOWc671pa1xgiGAEIAJQAaZn8uXgWeBd4N3g3eFf4d/iX/PtY8qwCnAGQAQgBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIQgBDAGQAiQCSM39f/iX+Hd4N3g3eDb4Nvg2eDT9HLRIBACEAWca+9z3nPOcc5xznHOcc5/zeHOcc5/ze/N783vze+97b3rvWu9bb3tveu9a71rvW29783prWXe+nMQAAAQCkEBzf2956zlnOWc5ZzlnOOcY5xhjGOcY5xjnGGL74vRjGGL74vfi92L34vRi++L34vdi9173Xtde117W3tbe1l7WXtZe1l7V2rbe1971CCEMAYghjAAAALWN+73rOmtZ6znrOmtaa1prWmtaa1prWu9a71rvWu9bc1tzW/Nb83vze/Nbc1tzW3Nbc1tvW3N783tze29783vze/N783vve+97b3r73MnwAAKIQ/N6e933vfe9d713vfe+e93nOOb4avhu+HL4cvhu+Gr75vfm9HOcc59vW297b3rvWPOd2rQAAhABlAJetHOdZznrOes6a1lnGfe8VnQIAb2Pf/7vWu9bb3rvWes671rrW6imHAMcAAACHAJxe3w0+BV4FXgV+BX4FngWeBf829CuLAIUARABiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghCCEQAZACqADEzP0+eBb4NngW+BX4FfgVeBV4FHz9OEgIAIAA5xn3v/N783vze/N783vze297b1rvWu9a71tvW29671rvWmtZ6zprWu9bb3rvWmtaa1rvWmtZd7zCEAAAAAKtSfu9ZzprWWc45xlnGOcY5xhnGGL74vfi9+L3Yvfi9GL74vfi9+L3Yvdi9+L3Yvde1t7W3tbe1l7WXrbe1t7WXtXatdq12rXatVqWa1ug5AQBCCEMAAAAtYz3nOcZ6zlnOWcZZxlnGes56znrOms6azprOu9a71pvWu9a71rvWu9aazprWmta71rvWu9a71tvWu9a71rvWu9a71rvW297b3rvWnvcxfAAACEK/9zznXe9d5z3nXe895zFLbwH1AVoCvAJeA94DXQO8AjkCUAEsMvveHOf73vze+9485xjGIAAhAMcAqyEc59vees6a1prWes56zl3vEXw95zzn297b3tve297b3l3vD1OpACsBIgAAAOsAvV5eBb4E3gT+BB4FPgVeBT4F/y6zG2kAZABDAGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIQwBkAKsAMzMfTz4FPgUeBV4FHgU+Bf4E/gT/PlASAgAAADnGXe/b3vve+9783vzeHN9d7z3nPOcc5xznPOcc5xznHOcc59vemtaa1rvWu9aa1prWu9Z6zvzeda0AAAAAEIQ95znGWc5ZxjnGWc5ZzhjGOcaa1prWes56znnOGcbYvde9173Yvfi9GL74vRjGOcYYxhjGGMYYxvi9t7WWrbe1l7WWrZatlq12rZrObWsAAEIAQgAAAC1jPecZxlnGOcYZxhnGGL45xlnOOcY5xjnGOcZZxnrOes56zlrGOcY5xjnGWcZ6znrOes5azlrOes6azprWms671prWmtaa1rvWmtZ97xB8AABNa9//+9783vze295+9zJ8ywBWArgCegNaBBoFuwU7BTsEOQNWAskA05x+79ve/N783hznGMZjAAAAxgALAc5KffdazprOmtaa1prWu9ae9xzn+9783tve29671n33M3TrALEBZQAAAAEADQGdTj4FXgS+BF4EvgS+BP4EvgR/HtUbaQBlAEIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghCAGUAiwCWO99G3gT+BL4E3gTeBN4EngSeBN9G8AkCACAAOcY957vW297b3tve/N661jWdFZ0VnTadNp02nTedV51XnTadt7W61prWms6aznrOes56zlrOu9b4vSEAAAAUnTzn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FAKwAUwBCgHIAEUApwCnAMcA6QArAW0BsAHyARQCNArSAQwBhQBiCGIIYghiCGIIYgjJAC4BNAI1AvMB8QGwAW0BCgHIAKcApwBFAIcA9QG1AfkBngKfA/8EHwZfBn8FXwQ/A58CGwK2AXMB7wDrAGYAQwBiCGIIYghiCEMAZgDLAFMBlgH6AV4CXwN/BH8FfgZ+Bn8FfwR/A78COwLXAXQBUgEvAe0ALQHIAIYAiwGLAe0BbwLSAhQDWBPaE/cCcQFjAGIIYghiCGIIYghiCGQAygA0AhoU+xN4AzYD9AKRAi8CDgLHAIcA1AKYG5kLVgMUA9ECkAJPAukAygBiCGIIYghjAC0BmQs7FLkLVgMUA9ICcAJPAmsBiACoAGIIYghiCIYALQE4C/sTuhNXAxQDsQJPAk4CSgGHAIUAYghiCGIIZACHAEYAaACKAMwADgExAXIBkwGTAbEBLAFCAGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYwDqAIkAqwDtAC8BUgGTAXMBMAHtACwByQBCCGIIQgBkAKcAygAMAS0BcAHTATYCVwJXAjUCsgFvAS0B6wDJAKgAhgBmAGYAhgCHAMkA6gAMAQwBLQENAS0ByABiCGIIYghiCGIIYghiCGIILAEtAU4BTgEtAQwB6gCoAIcAhgBmAGYARADKAJEBswE3ArkCWwO8A9wDvAMaA3cC9QGzAXEBTgEMAaoAZgBDAGIIYghiCGIIQgBFAMsATwGSAdQBFwKYAhoDnAMdBB4EnAP6ApkCFgKzAXEBTwEuAQwB6wDJAGUARAAkAEYAZwCJAKsADgFQAVABLgGmAGIIYghiCGIIYghiCGIIQwDJAC4BUgFzATABDgHsAKoAiQCIAIcAyQAMAQ4BLwEOAewAqwCJAGgAhwBiCGIIYghiCGMACwEvAVEBUQEvAe0AywCJAIgAqACGAGIIYghiCGIIhwAsAU8BUQFQATABDgHLAIkAZwCIAIQAYghiCGIIYghiCGIIYghjAGMAYwiDAIMApACDCIMIYwBjAGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghjAGMAgwiDCKQIhAiDCGMIYwhiCGIIYghiCGIIYghiCGIIYghiCGMIgwiDCIMIgwhiCGIIYghiCGIIYghCCEIIQghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghCCEIIQgBmAGUAZgCoAOsALgFRAXQBdAFSAS8B7ACpAGYARQBEAEQAIwAjAEMAYghiCGIIYghCAEQARABFAIcAqQDLAO0AMAF0AZYBlgF0AVEBDgHLAKkAhwBmAEQARABEAEQARQBDAGIAYgBiAGMAYwCDCIMIgwBiCGIIYghiCGIIYghiCGIIYghiCGMAYwCDCIMIgwiDCIMIYwBjAGIIYghiAGMAgwiDCIMIYwhjCGMAYghiCGIIYghiCGIIYghjAGMIgwiDCIMIgwhjAGMAYwBiCGIIYghiCGIIYghiCGMAYwiDCIMIgwiDAGMAYwBiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCEIIYwBkAIYApgDoACkBSgFqAUoBKQHnAKYAhQBkAEMAQwBCAEIAQgBiCGIIYghiCEIIQwBDAGQAhQCmAMcA6AAJASoBSwFLAUsBCQHoAMcApgCFAGUAZABDAEMAQwBCCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYghiCGIIYgg=";
static uint16_t *uiDecode(const char *b64, int w, int h) {
  uint16_t *px = (uint16_t *)malloc((size_t)w * h * 2);
  if (!px) return nullptr;
  uint8_t *o = (uint8_t *)px;
  size_t n = (size_t)w * h * 2, k = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (const char *c = b64; *c && k < n; c++) {
    int v = (*c >= 'A' && *c <= 'Z') ? *c - 'A' : (*c >= 'a' && *c <= 'z') ? *c - 'a' + 26 : (*c >= '0' && *c <= '9') ? *c - '0' + 52 : (*c == '+') ? 62 : (*c == '/') ? 63 : -1;
    if (v < 0) continue;
    acc = (acc << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; o[k++] = (acc >> bits) & 0xFF; }
  }
  return px;
}
// Wordmark splash: decoded, drawn and freed again
static void ui_wordmark(int cx, int cy) {
  uint16_t *px = uiDecode(WM_B64, WM_W, WM_H);
  if (!px) return;
  tft.pushImage(cx - WM_W / 2, cy - WM_H / 2, WM_W, WM_H, (const lgfx::rgb565_t *)px);
  free(px);
}
// ------------------------------------------------------------ cached text drawing
enum {
  SL_CHIP, SL_TEMP, SL_HASH, SL_UNIT, SL_CORE0, SL_CORE1, SL_CORE2, SL_CORE3, SL_GOOD, SL_BAD,
  SL_UP, SL_EXTRA, SL_FL, SL_FR, SL_INFO0 /* 8 slots */ = 14
};
#define UI_SLOTS 24
static char uiCache[UI_SLOTS][40];
static uint16_t uiCol[UI_SLOTS];
static const lgfx::IFont *uiFnt[UI_SLOTS];

static void uiForce() {
  for (int i = 0; i < UI_SLOTS; i++) { uiCache[i][0] = 0x01; uiCol[i] = 0; uiFnt[i] = nullptr; }
}

// Draws text only when it (or its colour) changed; the padding clears what was there before.
// clr > 0: the slot can switch between a big and a small font, so when the font changes the whole
// slot (w x clr pixels) is cleared first, otherwise the lower part of the old big text would stay.
static void uiPut(int slot, int x, int y, int w, const char *s, const lgfx::IFont *f, uint16_t fg, uint16_t bg, lgfx::textdatum_t d, int clr = 0) {
  char tmp[40];
  strlcpy(tmp, s, sizeof(tmp));
  if (!strcmp(uiCache[slot], tmp) && uiCol[slot] == fg) return;
  strlcpy(uiCache[slot], tmp, sizeof(uiCache[slot]));
  uiCol[slot] = fg;
  if (clr && uiFnt[slot] != f) {
    int x0 = d == UD_TR ? x - w : (d == UD_TC ? x - w / 2 : x);
    tft.fillRect(x0, y, w, clr, bg);
  }
  uiFnt[slot] = f;
  tft.setFont(f);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(d);
  tft.setTextPadding(w);
  tft.drawString(tmp, x, y);
  tft.setTextPadding(0);
}

// Picks the big font if the text fits in maxw pixels, otherwise the small one.
static const lgfx::IFont *uiFit(const char *s, int maxw, const lgfx::IFont *big, const lgfx::IFont *small) {
  tft.setFont(big);
  return tft.textWidth(s) <= maxw ? big : small;
}

static void ui_uptime_short(uint32_t u, char *o, size_t n) {
  unsigned d = u / 86400, h = u % 86400 / 3600, m = u % 3600 / 60, sec = u % 60;
  if (d) snprintf(o, n, "%ud %02uh", d, h);
  else if (h) snprintf(o, n, "%uh %02um", h, m);
  else snprintf(o, n, "%um %02us", m, sec);
}

// ------------------------------------------------------------ drawing pieces
static void uiCard(int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 10, UC_CARD);
  tft.drawRoundRect(x, y, w, h, 10, UC_EDGE);
}

static void uiRing(int x, int y, int w, int h, int r, uint16_t c) {  // 2 px rounded outline
  tft.drawRoundRect(x, y, w, h, r, c);
  tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r - 1, c);
}

static void uiGradLine(int y, int th) {  // orange -> magenta accent line across the screen
  for (int x = 0; x < 320; x++) {
    uint8_t r = 255 - (255 - 214) * x / 319, g = 155 - (155 - 100) * x / 319, b = 255 * x / 319;
    tft.drawFastVLine(x, y, th, UC(r, g, b));
  }
}

// ------------------------------------------------------------ screens
#define UI_GX 10   // graph position and size (inside its card)
#define UI_GY 189
#define UI_GW 300
#define UI_GH 30
static int uiPage = 0;
static bool uiNeedFull = true;
static float uiHist[UI_GW / CYD_GRAPH_PX];
#define UI_GN ((int)(sizeof(uiHist) / sizeof(uiHist[0])))
static float uiSmooth = 0;
static uint32_t uiTick = 0;  // samples taken so far (keeps the graph grid scrolling with the data)
static uint32_t uiHistT = 0, uiLastAcc = 0, uiCheerUntil = 0, uiLedUntil = 0, uiLastRej = 0;
static bool uiWasTouched = false, uiGraphDirty = true;
// screen sleep: tapping the miner graphic starts a timer, then the backlight goes off (mining and the LED go on as usual)
#ifndef CYD_SLEEP_MS
#define CYD_SLEEP_MS 120000UL   // 2 minutes
#endif
static bool uiScreenOff = false, uiSwallow = false;
static uint32_t uiOffAt = 0, uiPressUntil = 0;   // uiOffAt = 0: no timer running
static int32_t uiTx = 0, uiTy = 0;               // where the finger went down
// On the ST7789 board the touch sensor reports the position turned 180 degrees: flip it back (set 0 if taps land mirrored the other way)
#ifndef CYD_TOUCH_FLIP
#ifdef CYD_PANEL_ST7789
#define CYD_TOUCH_FLIP 1
#else
#define CYD_TOUCH_FLIP 0
#endif
#endif
static bool uiInMascot(int32_t x, int32_t y) { return x >= 0 && x <= 112 && y >= 28 && y <= 138; }
static uint32_t uiMascotKey = 0xFFFFFFFF;
static int32_t uiLogoCol = -1;
static int uiChipState = -1, uiWifiLvl = -1;
static lgfx::LGFX_Sprite gspr(&tft);  // graph sprite (flicker-free scrolling)
static bool uiGsprOk = false;

static void ui_begin() {
  pinMode(CYD_LED_R, OUTPUT);
  pinMode(CYD_LED_G, OUTPUT);
  pinMode(CYD_LED_B, OUTPUT);
  ui_led(false, false, false);
  tft.init();
  tft.setRotation(CYD_ROTATION);
  tft.setBrightness(CYD_BACKLIGHT);
  tft.fillScreen(UC_BG);
  mspr.setColorDepth(16);
  uiSprOk = (mspr.createSprite(96, 96) != nullptr);
  gspr.setColorDepth(16);
  uiGsprOk = (gspr.createSprite(UI_GW, UI_GH) != nullptr);
  ui_wordmark(tft.width() / 2, tft.height() / 2 - 8);   // HexOhm splash
  tft.setTextDatum(UD_TC);
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextColor(UC_MUT, UC_BG);
  tft.drawString("CRSminer", tft.width() / 2, tft.height() / 2 + 40);
  delay(2000);
  tft.fillScreen(UC_BG);
}

static void ui_splash(const char *title, const char *line1, const char *line2) {
  tft.fillScreen(UC_BG);
  uiGradLine(0, 3);
  uiMascot(&tft, 112, 14, UI_WORK, false, 0);
  uiRing(110, 12, 100, 100, 16, UC_ACC);
  tft.setTextDatum(UD_TC);
  tft.setFont(&lgfx::fonts::Font4);
  tft.setTextColor(UC_ACC, UC_BG);
  tft.drawString(title, 160, 122);
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextColor(UC_FG, UC_BG);
  tft.drawString(line1, 160, 160);
  tft.setTextColor(UC_MUT, UC_BG);
  tft.drawString(line2, 160, 184);
  uiGradLine(237, 3);
  uiNeedFull = true;
}

static void ui_portal(const char *ap, const char *ip) {
  tft.fillScreen(UC_BG);
  uiGradLine(0, 3);
  uiMascot(&tft, 8, 10, UI_WORK, false, 0);
  uiRing(6, 8, 100, 100, 16, UC_ACC);
  tft.setTextDatum(UD_TL);
  tft.setFont(&lgfx::fonts::Font4);
  tft.setTextColor(UC_ACC, UC_BG);
  tft.drawString("SETUP MODE", 120, 20);
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextColor(UC_MUT, UC_BG);
  tft.drawString("Use your phone or PC", 120, 56);
  tft.drawString("and follow these steps:", 120, 74);
  uiCard(6, 116, 308, 50);
  tft.setTextColor(UC_MUT, UC_CARD);
  tft.drawString("1. Join this WiFi network:", 16, 120);
  tft.setFont(&lgfx::fonts::Font4);
  tft.setTextColor(UC_FG, UC_CARD);
  tft.drawString(ap, 16, 138);
  uiCard(6, 172, 308, 50);
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextColor(UC_MUT, UC_CARD);
  tft.drawString("2. Open this address in a browser:", 16, 176);
  tft.setFont(&lgfx::fonts::Font4);
  tft.setTextColor(UC_OK, UC_CARD);
  tft.drawString(ip, 16, 194);
  uiNeedFull = true;
}

static void uiHeader(const UiSnap &s) {
  tft.fillRect(0, 0, 320, 26, UC_BAR);
  uiGradLine(26, 2);
  uiDrawLogo(5, 1);  // brand logo, upper left
  const int tx = 5 + UI_LOGO_W + 6;
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextDatum(UD_TL);
  tft.setTextPadding(0);
  tft.setTextColor(UC_ACC, UC_BAR);
  tft.drawString(s.title, tx, 5);
  int hx = tx + tft.textWidth(s.title) + 8;
  char hn[40];
  strlcpy(hn, s.host, sizeof(hn));
  char *dot = strstr(hn, ".local");  // the full name is on the info page
  if (dot) *dot = 0;
  tft.setTextColor(UC_MUT, UC_BAR);
  tft.drawString(hn, hx, 5);
}

// ONLINE / OFFLINE pill, top right
static void uiChip(bool ok) {
  if (uiChipState == (int)ok) return;
  uiChipState = (int)ok;
  uint16_t fill = ok ? UC(16, 58, 40) : UC(72, 24, 30), fg = ok ? UC_OK : UC_BAD;
  tft.fillRoundRect(244, 4, 70, 18, 9, fill);
  tft.fillCircle(256, 13, 3, fg);
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextDatum(UD_TL);
  tft.setTextPadding(0);
  tft.setTextColor(fg, fill);
  tft.drawString(ok ? "ONLINE" : "OFFLINE", 264, 5);
}

// four WiFi signal bars in the header
static void uiWifi(int rssi) {
  int lvl = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
  if (lvl == uiWifiLvl) return;
  uiWifiLvl = lvl;
  uint16_t on = lvl >= 3 ? UC_OK : (lvl == 2 ? UC_GOLD : UC_BAD), off = UC(60, 68, 90);
  tft.fillRect(212, 3, 22, 20, UC_BAR);
  for (int b = 0; b < 4; b++) {
    int h = 4 + b * 4;
    tft.fillRect(214 + b * 5, 20 - h, 3, h, b < lvl ? on : off);
  }
}

static void uiStaticMain(const UiSnap &s) {
  // hashrate tile
  uiCard(112, 32, 202, 100);
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextDatum(UD_TL);
  tft.setTextPadding(0);
  tft.setTextColor(UC_MUT, UC_CARD);
  tft.drawString("HASHRATE", 124, 38);
  // per-core colour dots
  static const uint16_t coreCol[4] = {UC_CYAN, UC_MAG, UC_GOLD, UC_OK};
  int n = s.nCores > 4 ? 4 : s.nCores;
  int sw = n ? 182 / n : 182;
  for (int i = 0; i < n; i++) tft.fillCircle(124 + i * sw + 3, 120, 3, coreCol[i]);
  // three stat cards
  for (int i = 0; i < 3; i++) uiCard(6 + i * 104, 138, 100, 44);
  char lab[16];
  strlcpy(lab, s.extraL, sizeof(lab));
  for (char *p = lab; *p; p++) *p = toupper((unsigned char)*p);
  tft.setFont(&lgfx::fonts::Font0);
  tft.setTextColor(UC_MUT, UC_CARD);
  tft.drawString("SHARES", 16, 144);
  tft.drawString("UPTIME", 120, 144);
  tft.drawString(lab, 224, 144);
  // graph card
  uiCard(6, 186, 308, 36);
  uiGraphDirty = true;
  uiLogoCol = -1;
}

static void uiStaticInfo(const UiSnap &s) {
  (void)s;
  uiCard(6, 32, 308, 186);
  static const char *labels[] = {"Host", "IP", "MAC", "Pool", "WiFi", "Chip", "Cores", "Status"};
  tft.setFont(&lgfx::fonts::Font2);
  tft.setTextDatum(UD_TL);
  tft.setTextPadding(0);
  tft.setTextColor(UC_MUT, UC_CARD);
  for (int i = 0; i < 8; i++) {
    int y = 40 + i * 22;
    tft.drawString(labels[i], 16, y);
    if (i) tft.drawFastHLine(14, y - 3, 292, UC_EDGE);
  }
  tft.setTextDatum(UD_TC);
  tft.setTextColor(UC_MUT, UC_BG);
  tft.drawString("tap anywhere to go back", 160, 224);
}

static void uiStaticPage(const UiSnap &s) {
  tft.fillScreen(UC_BG);
  uiHeader(s);
  if (uiPage == 0) uiStaticMain(s);
  else uiStaticInfo(s);
  uiForce();
  uiMascotKey = 0xFFFFFFFF;
  uiChipState = -1;
  uiWifiLvl = -1;
  uiNeedFull = false;
}

// Faint grid: three horizontal lines and a vertical line every UI_GRID_N samples (2 s at the default speed).
// The vertical lines scroll with the data, so you can see the movement even when the hashrate is flat.
#define UI_GRID_N 16
static int16_t uiBH[UI_GW / CYD_GRAPH_PX];

static void uiDrawGraph() {
  lgfx::LovyanGFX *g = uiGsprOk ? (lgfx::LovyanGFX *)&gspr : (lgfx::LovyanGFX *)&tft;
  const int ox = uiGsprOk ? 0 : UI_GX, oy = uiGsprOk ? 0 : UI_GY;
  const int P = CYD_GRAPH_PX, base = oy + UI_GH;
  g->fillRect(ox, oy, UI_GW, UI_GH, UC_CARD);
  float mx = 1;
  for (int i = 0; i < UI_GN; i++) if (uiHist[i] > mx) mx = uiHist[i];
  mx *= 1.15f;
  const uint16_t hi = UC(190, 104, 0), lo = UC(105, 58, 0), cap = UC_GOLD;
  const uint16_t gridBg = UC(36, 45, 68), gridHi = UC(214, 130, 26), gridLo = UC(132, 78, 16);  // grid over empty area / over the bars
  for (int i = 0; i < UI_GN; i++) {
    int h = (int)(uiHist[i] / mx * (UI_GH - 2));
    if (h < 1 && uiHist[i] > 0) h = 1;
    uiBH[i] = h;
    if (h <= 0) continue;
    int x = ox + i * P, top = base - h, hiH = h - h / 2;
    g->fillRect(x, top, P, hiH, hi);                 // lighter upper half
    g->fillRect(x, top + hiH, P, h / 2, lo);         // darker lower half
    g->fillRect(x, top, P, h < 2 ? h : 2, cap);      // bright top edge
  }
  for (int k = 1; k <= 3; k++) {  // horizontal lines at 25 / 50 / 75 %
    int y = oy + UI_GH * k / 4, up = base - y;
    for (int i = 0; i < UI_GN; i++) {
      int top = base - uiBH[i];
      uint16_t c = uiBH[i] >= up ? (y < top + (uiBH[i] - uiBH[i] / 2) ? gridHi : gridLo) : gridBg;
      g->fillRect(ox + i * P, y, P, 1, c);
    }
  }
  for (int i = 0; i < UI_GN; i++) {  // vertical lines, scrolling with the samples
    int32_t n = (int32_t)uiTick - (UI_GN - 1 - i);
    if (n < 0 || n % UI_GRID_N) continue;
    int x = ox + i * P, h = uiBH[i], top = base - h, hiH = h - h / 2;
    if (h < UI_GH) g->drawFastVLine(x, oy, UI_GH - h, gridBg);
    if (h > 0) {
      g->drawFastVLine(x, top, hiH, gridHi);
      g->drawFastVLine(x, top + hiH, h / 2, gridLo);
    }
  }
  if (uiGsprOk) gspr.pushSprite(UI_GX, UI_GY);
}

// Call every ~60 ms from a task. Handles touch (tap = switch page), LED and redraws.
static void ui_update(const UiSnap &s) {
  uint32_t now = millis();
  char b[48], b2[48];

  // touch: a tap (finger lifted) toggles between the status page and the info page
  int32_t tx, ty;
  bool touched = tft.getTouch(&tx, &ty) > 0;
  if (touched && !uiWasTouched) {                      // finger went down
    uiTx = tx; uiTy = ty;
#if CYD_TOUCH_FLIP
    uiTx = tft.width() - 1 - tx; uiTy = tft.height() - 1 - ty;
#endif
    if (uiScreenOff) {                                 // screen is off: any touch lights it up, and is not used for anything else
      uiScreenOff = false; uiOffAt = 0; uiSwallow = true;
      tft.setBrightness(CYD_BACKLIGHT);
    }
  }
  if (!touched && uiWasTouched) {                      // finger lifted = a tap
    if (uiSwallow) uiSwallow = false;
    else if (uiPage == 0 && uiInMascot(uiTx, uiTy)) {  // tap on the miner graphic: press effect, screen turns off after 2 minutes
      uiPressUntil = now + 250;
      uiOffAt = now + CYD_SLEEP_MS;
      if (!uiOffAt) uiOffAt = 1;
    } else { uiPage = (uiPage + 1) % 2; uiNeedFull = true; }
  }
  uiWasTouched = touched;
  if (uiOffAt && !uiScreenOff && (int32_t)(now - uiOffAt) >= 0) { uiScreenOff = true; uiOffAt = 0; tft.setBrightness(0); }

  // graph samples: eased towards the real value so it glides instead of stepping once a second
  if (now - uiHistT >= (uint32_t)CYD_GRAPH_MS) {
    uiHistT = now;
    if (uiSmooth < 1 && s.hr > 0) uiSmooth = s.hr;
    uiSmooth += (s.hr - uiSmooth) * 0.25f;
    memmove(uiHist, uiHist + 1, sizeof(uiHist) - sizeof(uiHist[0]));
    uiHist[UI_GN - 1] = uiSmooth;
    uiTick++;
    uiGraphDirty = true;
  }
  if (s.acc != uiLastAcc) { uiLastAcc = s.acc; uiCheerUntil = now + 1800; uiLedUntil = now + 150; ui_led(false, true, false); }
  if (s.rej != uiLastRej) { uiLastRej = s.rej; uiLedUntil = now + 150; ui_led(true, false, false); }
  if (uiLedUntil && now > uiLedUntil) { uiLedUntil = 0; ui_led(false, false, false); }
  if (!s.ok && !uiLedUntil && (now / 100) % 20 == 0) { ui_led(true, false, false); uiLedUntil = now + 80; }  // slow red blink offline

  if (uiNeedFull) uiStaticPage(s);

  // header status (both pages)
  uiChip(s.ok);
  uiWifi(s.rssi);

  uint32_t u = s.up;
  snprintf(b, sizeof(b), "%ud %02uh %02um", (unsigned)(u / 86400), (unsigned)(u % 86400 / 3600), (unsigned)(u % 3600 / 60));

  if (uiPage == 0) {
    // logo button: the mascot inside a ring that shows the state (orange working, green share, red offline, white pressed)
    int mood = !s.ok ? UI_SLEEP : (now < uiCheerUntil ? UI_CHEER : UI_WORK);
    bool blink = (now % 3600) < 160;
    int frame = (int)(now / 250);
    uint32_t mkey = ((uint32_t)(frame & 0xFFFF) << 4) | ((uint32_t)mood << 1) | (blink ? 1 : 0);
    if (mkey != uiMascotKey) {  // only redraw the mascot when something about it changed
      uiMascotKey = mkey;
      if (uiSprOk) {
        uiMascot(&mspr, 0, 0, mood, blink, frame);
        mspr.pushSprite(8, 34);
      } else {
        uiMascot(&tft, 8, 34, mood, blink, frame);
      }
      uiLogoCol = -1;  // the sprite's square corners overwrite the ring arcs: redraw the ring below
    }
    uint16_t ring = (touched || now < uiPressUntil) ? UC_FG : (!s.ok ? UC_BAD : (now < uiCheerUntil ? UC_OK : UC_ACC));
    if ((int32_t)ring != uiLogoCol) { uiLogoCol = ring; uiRing(6, 32, 100, 100, 16, ring); }

    // hashrate tile (number area ends at x=258, unit area starts after it)
    const char *unit;
    ui_rate(s.hr, b2, sizeof(b2), &unit);
    // the number's background fill reaches under the unit text: draw the number first, then (re)draw the unit on top
    char hprev[40];
    strlcpy(hprev, uiCache[SL_HASH], sizeof(hprev));
    uiPut(SL_HASH, 122, 54 + UI_HASH_DY, 136, b2, &lgfx::fonts::Font6, UC_ACC, UC_CARD, UD_TL, 48);  // always the same size (the one used for kH/s)
    if (strcmp(hprev, uiCache[SL_HASH])) uiCache[SL_UNIT][0] = 0x01;  // number was repainted: force the unit to repaint
    uiPut(SL_UNIT, 306, 78, 54, unit, &lgfx::fonts::Font4, UC_CYAN, UC_CARD, UD_TR);
    snprintf(b2, sizeof(b2), "%.0fC", s.temp);
    uiPut(SL_TEMP, 306, 38, 40, b2, &lgfx::fonts::Font2, s.temp < 65 ? UC_OK : (s.temp < 80 ? UC_GOLD : UC_BAD), UC_CARD, UD_TR);
    static const uint16_t coreCol[4] = {UC_CYAN, UC_MAG, UC_GOLD, UC_OK};
    int n = s.nCores > 4 ? 4 : s.nCores;
    int sw = n ? 182 / n : 182;
    for (int i = 0; i < n; i++) {
      char t[12];
      ui_si(s.cores[i], t, sizeof(t));
      uiPut(SL_CORE0 + i, 124 + i * sw + 10, 112, sw - 10, t, &lgfx::fonts::Font2, UC_FG, UC_CARD, UD_TL);
    }

    // stat cards
    snprintf(b2, sizeof(b2), "%u", (unsigned)s.acc);
    uiPut(SL_GOOD, 14, 154, 84, b2, &lgfx::fonts::Font4, UC_OK, UC_CARD, UD_TL);
    snprintf(b2, sizeof(b2), "BAD %u", (unsigned)s.rej);
    uiPut(SL_BAD, 100, 144, 42, b2, &lgfx::fonts::Font0, s.rej ? UC_BAD : UC_MUT, UC_CARD, UD_TR);
    ui_uptime_short(u, b2, sizeof(b2));
    uiPut(SL_UP, 118, 154, 88, b2, uiFit(b2, 88, &lgfx::fonts::Font4, &lgfx::fonts::Font2), UC_CYAN, UC_CARD, UD_TL, 26);
    uiPut(SL_EXTRA, 222, 154, 88, s.extraV, uiFit(s.extraV, 88, &lgfx::fonts::Font4, &lgfx::fonts::Font2), UC_GOLD, UC_CARD, UD_TL, 26);

    if (uiGraphDirty) { uiGraphDirty = false; uiDrawGraph(); }

    // footer: error (red) if any, else pool; IP on the right
    if (s.err[0]) uiPut(SL_FL, 8, 224, 220, s.err, &lgfx::fonts::Font2, UC_BAD, UC_BG, UD_TL);
    else uiPut(SL_FL, 8, 224, 220, s.pool, &lgfx::fonts::Font2, UC_MUT, UC_BG, UD_TL);
    uiPut(SL_FR, 312, 224, 90, s.ip, &lgfx::fonts::Font2, UC_MUT, UC_BG, UD_TR);
  } else {
    const lgfx::IFont *f = &lgfx::fonts::Font2;
    uiPut(SL_INFO0 + 0, 76, 40, 230, s.host, f, UC_CYAN, UC_CARD, UD_TL);
    uiPut(SL_INFO0 + 1, 76, 62, 230, s.ip, f, UC_FG, UC_CARD, UD_TL);
    uiPut(SL_INFO0 + 2, 76, 84, 230, s.mac, f, UC_FG, UC_CARD, UD_TL);
    uiPut(SL_INFO0 + 3, 76, 106, 230, s.pool, f, UC_GOLD, UC_CARD, UD_TL);
    snprintf(b2, sizeof(b2), "%d dBm", s.rssi);
    uiPut(SL_INFO0 + 4, 76, 128, 230, b2, f, s.rssi >= -70 ? UC_OK : (s.rssi >= -80 ? UC_GOLD : UC_BAD), UC_CARD, UD_TL);
    snprintf(b2, sizeof(b2), "%.0f C, %u KB free heap", s.temp, (unsigned)(s.heap / 1024));
    uiPut(SL_INFO0 + 5, 76, 150, 230, b2, f, UC_FG, UC_CARD, UD_TL);
    snprintf(b2, sizeof(b2), "%d mining core(s), up %s", s.nCores, b);
    uiPut(SL_INFO0 + 6, 76, 172, 230, b2, f, UC_FG, UC_CARD, UD_TL);
    uiPut(SL_INFO0 + 7, 76, 194, 230, s.err[0] ? s.err : (s.ok ? "mining" : "waiting for pool"), f,
          s.err[0] ? UC_BAD : UC_OK, UC_CARD, UD_TL);
  }
}


#include "sdkconfig.h"
#include <esp_system.h>
#if defined(CONFIG_IDF_TARGET_ESP32)
#define CRS_HW_SHA 1
#include <soc/dport_reg.h>
#include <soc/hwcrypto_reg.h>
#if __has_include(<esp_private/periph_ctrl.h>)
#include <esp_private/periph_ctrl.h>
#define CRS_HAVE_PERIPH 1
#elif __has_include(<driver/periph_ctrl.h>)
#include <driver/periph_ctrl.h>
#define CRS_HAVE_PERIPH 1
#endif
#if __has_include(<sha/sha_parallel_engine.h>)
#include <sha/sha_parallel_engine.h>
#define CRS_SHA_LOCK 1
#endif
#endif

// Default LED pin (changeable at run time in the setup portal / dashboard): 2 = blue LED on most
// classic ESP32 dev boards, -1 = none. CRS_RESERVED_PIN(p) lets a board variant protect its own pins.
// Per-board defaults, picked from the board selected in the Arduino IDE. You can always change the
// pin in the setup portal / dashboard. CRS_LED_ACTIVE_LOW = the LED lights when the pin is LOW.
#ifndef BLUE_LED_PIN
#if defined(ARDUINO_XIAO_ESP32C3)
#define BLUE_LED_PIN 10             // Seeed Studio XIAO ESP32C3: user LED, active low
#define CRS_LED_ACTIVE_LOW 1
#elif defined(ARDUINO_XIAO_ESP32C6)
#define BLUE_LED_PIN 15             // Seeed Studio XIAO ESP32C6: user LED, active low
#define CRS_LED_ACTIVE_LOW 1
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define BLUE_LED_PIN 8              // ESP32-C3 Super Mini: blue LED, active low
#define CRS_LED_ACTIVE_LOW 1
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#define BLUE_LED_PIN 15             // ESP32-C6 Super Mini: LED on GPIO15 (check your board)
#define CRS_LED_ACTIVE_LOW 1
#else
#define BLUE_LED_PIN 2              // classic ESP32 / S3 dev boards
#endif
#endif
#ifndef CRS_LED_ACTIVE_LOW
#define CRS_LED_ACTIVE_LOW 0
#endif
// BOOT button (hold 3 s at power-up = factory reset): GPIO0 on ESP32 / S3, GPIO9 on C3 / C6
#ifndef CRS_BOOT_PIN
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define CRS_BOOT_PIN 9
#else
#define CRS_BOOT_PIN 0
#endif
#endif
#ifndef CRS_RESERVED_PIN
#define CRS_RESERVED_PIN(p) false
#endif

#define MAX_WORKERS 4
#define BATCH 32768
#define DIFF1_TOP64 4294901760.0  // 0x00000000FFFF0000

// ------------------------------------------------------------ types (kept first
// so the Arduino auto-generated prototypes can see them)
struct Work {
  uint32_t mid[8];
  uint32_t hdr[20];  // header as big-endian words (hdr[19] = nonce slot, unused here) for the SHA engine
  uint32_t w0, w1, w2;
  uint32_t ntime;
  char jobId[64];
  char en2hex[33];
};
// context shared with the assembly loops - the field offsets are known to the assembly
struct PlCtx {
  uint32_t hdr[20];       // +0   header words, nonce slot at [19]
  uint32_t nonce;         // +80  in: first nonce (byte-swapped space); out: next one
  uint32_t budget;        // +84  nonces to try / left
  volatile uint32_t run;  // +88  cleared = stop
  uint32_t pad;           // +92  0x80000000
  uint32_t chk;           // +96  unused
};
typedef uint32_t (*PlFn)(volatile uint32_t *, PlCtx *);
struct Cfg {
  String name = "";  // mandatory 4-character miner ID -> crsminer-<name>.local
  String pool = "solo.ckpool.org:3333";
  String addr = "";
  String worker = "crs";
  String pass = "x";
  String sugg = "";     // optional mining.suggest_difficulty value (blank = pool default)
  uint8_t ledMode = 0;  // 0 = on (default behaviour), 1 = off (LED dark + quiet serial), 2 = diagnostic
  uint8_t lang = 0;     // 0 = English, 1 = Greek (dashboard + setup portal)
  int ledPin = BLUE_LED_PIN;  // GPIO of the status LED (-1 = none)
};
struct Job {
  String id;
  uint8_t prev[32];
  std::vector<uint8_t> cb1, cb2, branch, en1;
  int en2Size = 4;
  uint32_t version = 0, nbits = 0, ntime = 0;
};
struct Share {
  char job[64];
  char en2[33];
  uint32_t ntime, nonce;
};

// ------------------------------------------------------------ SHA-256 core
static DRAM_ATTR const uint32_t SHA_IV[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

static DRAM_ATTR const uint32_t SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define BS0(x) (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define BS1(x) (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SS0(x) (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define SS1(x) (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))
#define CH(e, f, g) ((g) ^ ((e) & ((f) ^ (g))))
#define MAJ(a, b, c) (((a) & (b)) | ((c) & ((a) | (b))))
#define SHA_RND(i)                                                         \
  {                                                                        \
    uint32_t t1 = h + BS1(e) + CH(e, f, g) + SHA_K[i] + W[i];              \
    uint32_t t2 = BS0(a) + MAJ(a, b, c);                                   \
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;     \
  }

// Runs the first n rounds. W[0..15] holds the block; W[16..] is filled in.
static inline __attribute__((always_inline)) void sha_rounds(uint32_t *W, uint32_t *v, int n) {
  uint32_t a = v[0], b = v[1], c = v[2], d = v[3], e = v[4], f = v[5], g = v[6], h = v[7];
  int i = 0;
#pragma GCC unroll 8
  for (; i < 16; i++) SHA_RND(i);
#pragma GCC unroll 8
  for (; i < n; i++) {
    W[i] = SS1(W[i - 2]) + W[i - 7] + SS0(W[i - 15]) + W[i - 16];
    SHA_RND(i);
  }
  v[0] = a; v[1] = b; v[2] = c; v[3] = d; v[4] = e; v[5] = f; v[6] = g; v[7] = h;
}

static inline uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void put32le(uint8_t *p, uint32_t v) {
  p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void IRAM_ATTR sha_compress(uint32_t st[8], const uint32_t blk[16]) {
  uint32_t W[64], v[8];
  memcpy(W, blk, 64);
  memcpy(v, st, 32);
  sha_rounds(W, v, 64);
  for (int i = 0; i < 8; i++) st[i] += v[i];
}

static void sha256_bytes(const uint8_t *d, size_t len, uint8_t out[32]) {
  uint32_t st[8], w[16];
  memcpy(st, SHA_IV, 32);
  size_t i = 0;
  for (; i + 64 <= len; i += 64) {
    for (int k = 0; k < 16; k++) w[k] = be32(d + i + 4 * k);
    sha_compress(st, w);
  }
  uint8_t blk[64] = {0};
  size_t rem = len - i;
  memcpy(blk, d + i, rem);
  blk[rem] = 0x80;
  if (rem >= 56) {
    for (int k = 0; k < 16; k++) w[k] = be32(blk + 4 * k);
    sha_compress(st, w);
    memset(blk, 0, 64);
  }
  uint64_t bits = (uint64_t)len * 8;
  for (int k = 0; k < 8; k++) blk[63 - k] = bits >> (8 * k);
  for (int k = 0; k < 16; k++) w[k] = be32(blk + 4 * k);
  sha_compress(st, w);
  for (int k = 0; k < 8; k++) {
    out[4 * k] = st[k] >> 24; out[4 * k + 1] = st[k] >> 16;
    out[4 * k + 2] = st[k] >> 8; out[4 * k + 3] = st[k];
  }
}

static void sha256d(const uint8_t *d, size_t len, uint8_t out[32]) {
  uint8_t t[32];
  sha256_bytes(d, len, t);
  sha256_bytes(t, 32, out);
}

// Scans [n0, n0+cnt). Returns true at the first nonce whose hash top 32 bits
// (leading zeros) are <= thr. Second SHA runs only 61 rounds: final 'h' == 'e' after round 60.
static bool IRAM_ATTR scan_nonces(const Work &w, uint32_t n0, uint32_t cnt, uint32_t thr, uint32_t *hit) {
  uint32_t W1[64], W2[64], v[8], u[8];
  W1[0] = w.w0; W1[1] = w.w1; W1[2] = w.w2; W1[4] = 0x80000000;
  for (int i = 5; i < 15; i++) W1[i] = 0;
  W1[15] = 640;
  W2[8] = 0x80000000;
  for (int i = 9; i < 15; i++) W2[i] = 0;
  W2[15] = 256;
  for (uint32_t n = 0; n < cnt; n++) {
    uint32_t nonce = n0 + n;
    W1[3] = __builtin_bswap32(nonce);
    memcpy(v, w.mid, 32);
    sha_rounds(W1, v, 64);
    for (int i = 0; i < 8; i++) W2[i] = v[i] + w.mid[i];
    memcpy(u, SHA_IV, 32);
    sha_rounds(W2, u, 61);
    if (__builtin_bswap32(u[4] + SHA_IV[7]) <= thr) { *hit = nonce; return true; }
  }
  return false;
}

static void IRAM_ATTR hash_full(const Work &w, uint32_t nonce, uint32_t *top, uint32_t *h6) {
  uint32_t W1[64], W2[64], v[8], u[8];
  W1[0] = w.w0; W1[1] = w.w1; W1[2] = w.w2; W1[3] = __builtin_bswap32(nonce);
  W1[4] = 0x80000000;
  for (int i = 5; i < 15; i++) W1[i] = 0;
  W1[15] = 640;
  memcpy(v, w.mid, 32);
  sha_rounds(W1, v, 64);
  for (int i = 0; i < 8; i++) W2[i] = v[i] + w.mid[i];
  W2[8] = 0x80000000;
  for (int i = 9; i < 15; i++) W2[i] = 0;
  W2[15] = 256;
  memcpy(u, SHA_IV, 32);
  sha_rounds(W2, u, 64);
  *top = __builtin_bswap32(u[7] + SHA_IV[7]);
  *h6 = __builtin_bswap32(u[6] + SHA_IV[6]);
}

// ------------------------------------------------------------ web page
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CRSminer</title>
<link rel="icon" href="data:image/png;base64,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">
<style>
:root{--bg:#0f1115;--card:#181b22;--fg:#e8eaf0;--mut:#8b92a5;--acc:#f7931a;--ok:#3ecf8e;--bad:#ff5c5c}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px system-ui,sans-serif;padding:14px;max-width:760px;margin:auto}
h1{font-size:20px;margin:6px 0 14px;display:flex;align-items:center;gap:8px;flex-wrap:wrap}h1 .lg{height:46px;width:auto}.foot .wm{height:36px;width:auto;margin-bottom:4px}h1 b{color:var(--acc)}h2{font-size:14px;color:var(--mut);margin:20px 0 8px;text-transform:uppercase;letter-spacing:.06em}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}@media(min-width:560px){.grid{grid-template-columns:repeat(3,1fr)}}
.card{background:var(--card);border-radius:10px;padding:12px}.card small{color:var(--mut);display:block;font-size:12px}
.card span{font-size:20px;font-weight:600}.big span{color:var(--acc);font-size:26px}
canvas{width:100%;height:250px;background:#050a0c;border:1px solid #1d2a2a;border-radius:10px;display:block;touch-action:pan-y}
label{display:block;color:var(--mut);font-size:12px;margin:10px 0 3px}
input,select{width:100%;padding:10px;border-radius:8px;border:1px solid #2b303b;background:#10131a;color:var(--fg);font-size:15px}
button{margin-top:14px;margin-right:8px;padding:10px 16px;border:0;border-radius:8px;background:var(--acc);color:#111;font-weight:600;font-size:15px}
button.s{background:#2b303b;color:var(--fg)}.ok{color:var(--ok)}.bad{color:var(--bad)}#msg{margin-top:10px;color:var(--mut)}
.diag{background:var(--card);border-radius:10px;padding:10px 12px;font:12px ui-monospace,Menlo,Consolas,monospace;color:var(--mut);word-break:break-all}
.diag div{margin:3px 0}.foot{margin:26px 0 8px;text-align:center;color:var(--mut);font-size:12px}.foot a{color:var(--mut)}
</style></head><body>
<h1><img class="lg" src="data:image/png;base64,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" alt="HexOhm"><span><b>CRS</b>miner</span> <small id="conn" class="bad" data-t="offline">offline</small></h1>
<div class="grid">
<div class="card big"><small data-t="Hashrate">Hashrate</small><span id="hr">-</span></div>
<div class="card"><small data-t="Shares ok / rejected">Shares ok / rejected</small><span id="sh">-</span></div>
<div class="card"><small data-t="Best difficulty">Best difficulty</small><span id="best">-</span></div>
<div class="card"><small data-t="Pool difficulty">Pool difficulty</small><span id="pd">-</span></div>
<div class="card"><small data-t="Est. time per share">Est. time per share</small><span id="eta">-</span></div>
<div class="card"><small data-t="Uptime">Uptime</small><span id="up">-</span></div>
<div class="card"><small data-t="Pool">Pool</small><span id="pool">-</span></div>
<div class="card"><small data-t="Per core">Per core</small><span id="cores">-</span></div>
<div class="card"><small data-t="WiFi RSSI">WiFi RSSI</small><span id="rssi">-</span></div>
<div class="card"><small data-t="Temp / Free heap">Temp / Free heap</small><span id="sys">-</span></div>
<div class="card"><small data-t="Hostname">Hostname</small><span id="host" style="font-size:15px">-</span></div>
<div class="card"><small data-t="IP address">IP address</small><span id="ip" style="font-size:15px">-</span></div>
<div class="card"><small data-t="MAC address">MAC address</small><span id="mac" style="font-size:15px">-</span></div>
</div>
<div id="stage" style="margin-top:10px;color:var(--mut)"></div>
<h2 data-t="Diagnostics">Diagnostics</h2>
<div class="diag"><div id="d_login"></div><div id="d_sent"></div><div id="d_rep"></div><div id="d_rst"></div></div>
<h2 data-t="Event log">Event log</h2><div class="diag" id="evlog">-</div>
<h2 data-t="Hashrate scope (last 5 min)">Hashrate scope (last 5 min)</h2><canvas id="ch" width="700" height="250"></canvas>
<h2 data-t="Settings">Settings</h2>
<form id="f">
<label data-t="Miner name (exactly 4 letters/digits → crsminer-NAME.local)">Miner name (exactly 4 letters/digits → crsminer-NAME.local)</label><input id="c_name" maxlength="4" minlength="4" pattern="[A-Za-z0-9]{4}" required autocomplete="off">
<label data-t="Pool URL (host:port)">Pool URL (host:port)</label><input id="c_pool" autocomplete="off">
<label data-t="Wallet address (must match the pool's coin: BTC, DGB, ...)">Wallet address (must match the pool's coin: BTC, DGB, ...)</label><input id="c_addr" autocomplete="off">
<label data-t="Worker name">Worker name</label><input id="c_worker" autocomplete="off">
<label data-t="Suggested pool difficulty (optional, e.g. 0.0001 - blank = pool default)">Suggested pool difficulty (optional, e.g. 0.0001 - blank = pool default)</label><input id="c_sugg" autocomplete="off" placeholder="blank" data-tp="blank">
<label data-t="Pool password (leave blank to keep)">Pool password (leave blank to keep)</label><input id="c_pass" type="password" autocomplete="off">
<label data-t="Blue LED">Blue LED</label><select id="c_led"><option value="0" data-t="On (default)">On (default)</option><option value="1" data-t="Off (also silences the serial status line)">Off (also silences the serial status line)</option><option value="2" data-t="Diagnostic (blinks 20 s, then off; steady on = needs attention)">Diagnostic (blinks 20 s, then off; steady on = needs attention)</option></select>
<label data-t="LED GPIO pin (2 = most boards; -1 = no LED pin)">LED GPIO pin (2 = most boards; -1 = no LED pin)</label><input id="c_ledpin" type="number" min="-1" max="48" step="1" value="2" autocomplete="off">
<label data-t="Language">Language</label><select id="c_lang"><option value="0">English</option><option value="1">Ελληνικά</option></select>
<button type="submit" data-t="Save &amp; reboot">Save &amp; reboot</button>
<button type="button" class="s" id="rw" data-t="Reset WiFi">Reset WiFi</button>
</form><div id="msg"></div>
<div class="foot"><img class="wm" src="data:image/png;base64,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" alt="HexOhm"><br><span data-t="© 2026 HexOhm">© 2026 HexOhm</span> &middot; <a href="/license" data-t="MIT License &amp; credits">MIT License &amp; credits</a></div>
<script>
const $=i=>document.getElementById(i);let hist=[],lang=0;
const EL={"offline": "εκτός σύνδεσης", "● connected": "● συνδεδεμένο", "● pool offline": "● pool εκτός σύνδεσης", "● offline": "● εκτός σύνδεσης", "Hashrate": "Ρυθμός hash", "Shares ok / rejected": "Shares αποδεκτά / απορριφθέντα", "Best difficulty": "Καλύτερη δυσκολία", "Pool difficulty": "Δυσκολία pool", "Est. time per share": "Εκτ. χρόνος ανά share", "Uptime": "Χρόνος λειτουργίας", "Pool": "Pool", "Per core": "Ανά πυρήνα", "WiFi RSSI": "Ισχύς WiFi (RSSI)", "Temp / Free heap": "Θερμοκρασία / Ελεύθερη μνήμη", "Hostname": "Όνομα συσκευής", "IP address": "Διεύθυνση IP", "MAC address": "Διεύθυνση MAC", "Status:": "Κατάσταση:", "Diagnostics": "Διαγνωστικά", "login": "σύνδεση", "sent:": "εστάλη:", "Event log": "Ιστορικό συμβάντων", "gentle power mode": "ήπια λειτουργία ισχύος", "power": "ισχύς", "gentle mode": "ήπια λειτουργία", "stable 24 h, power mode eased": "σταθερό 24 ω, χαλάρωση ήπιας λειτουργίας", "start": "εκκίνηση", "restart": "επανεκκίνηση", "recovered": "αποκαταστάθηκε", "last start:": "τελευταία εκκίνηση:", "power on": "ενεργοποίηση ρεύματος", "BROWNOUT (power supply too weak)": "BROWNOUT (ανεπαρκής τροφοδοσία)", "crash (panic)": "κατάρρευση (panic)", "watchdog reset": "επαναφορά watchdog", "reset button": "κουμπί reset", "software restart": "επανεκκίνηση λογισμικού", "WiFi lost for 10 min": "χάθηκε το WiFi για 10 λεπτά", "pool unreachable for 15 min": "το pool απρόσιτο για 15 λεπτά", "hashing stalled": "το hashing κόλλησε", "hardware SHA stalled": "το SHA υλικού κόλλησε", "low memory": "χαμηλή μνήμη", "reply:": "απάντηση:", "none yet": "κανένα ακόμη", "Hashrate scope (last 5 min)": "Παλμογράφος hash (τελευταία 5 λεπτά)", "NOW": "ΤΩΡΑ", "AVG": "ΜΕΣΟΣ", "MAX": "ΜΕΓ", "MIN": "ΕΛΑΧ", "Total": "Σύνολο", "Core": "Πυρήνας", "now": "τώρα", "accepted": "αποδεκτό", "rejected": "απορριφθέν", "Settings": "Ρυθμίσεις", "Miner name (exactly 4 letters/digits → crsminer-NAME.local)": "Όνομα miner (ακριβώς 4 γράμματα/ψηφία → crsminer-NAME.local)", "Pool URL (host:port)": "Διεύθυνση pool (host:port)", "Wallet address (must match the pool's coin: BTC, DGB, ...)": "Διεύθυνση πορτοφολιού (πρέπει να ταιριάζει με το νόμισμα του pool: BTC, DGB, ...)", "Worker name": "Όνομα worker", "LED GPIO pin (2 = most boards; -1 = no LED pin)": "Pin GPIO της LED (2 = οι περισσότερες πλακέτες· -1 = καμία)", "LED pin not allowed on this board": "Αυτό το pin LED δεν επιτρέπεται σε αυτή την πλακέτα", "Suggested pool difficulty (optional, e.g. 0.0001 - blank = pool default)": "Προτεινόμενη δυσκολία pool (προαιρετικό, π.χ. 0.0001 - κενό = προεπιλογή pool)", "blank": "κενό", "Pool password (leave blank to keep)": "Κωδικός pool (κενό = χωρίς αλλαγή)", "Blue LED": "Μπλε λυχνία LED", "On (default)": "Αναμμένη (προεπιλογή)", "Off (also silences the serial status line)": "Σβηστή (σιωπά και η σειριακή γραμμή κατάστασης)", "Diagnostic (blinks 20 s, then off; steady on = needs attention)": "Διαγνωστική (αναβοσβήνει 20 δλ, μετά σβήνει· σταθερά αναμμένη = χρειάζεται ενέργεια)", "Language": "Γλώσσα", "Save & reboot": "Αποθήκευση & επανεκκίνηση", "Reset WiFi": "Επαναφορά WiFi", "Erase WiFi credentials and reboot into setup mode?": "Διαγραφή στοιχείων WiFi και επανεκκίνηση σε λειτουργία ρύθμισης;", "Rebooting into setup AP...": "Επανεκκίνηση σε λειτουργία ρύθμισης...", "© 2026 HexOhm": "© 2026 HexOhm", "MIT License & credits": "Άδεια MIT & πιστώσεις", "starting": "εκκίνηση", "connecting to pool": "σύνδεση στο pool", "subscribing": "εγγραφή (subscribe)", "authorizing": "εξουσιοδότηση", "waiting for first job": "αναμονή για πρώτη εργασία", "mining": "εξόρυξη", "reconnecting": "επανασύνδεση", "disabled": "απενεργοποιημένο", "SHA self-test failed": "Απέτυχε ο αυτοέλεγχος SHA", "WiFi / internet not connected": "Δεν υπάρχει σύνδεση WiFi / internet", "pool refused the login (check wallet address)": "Το pool απέρριψε τη σύνδεση (ελέγξτε τη διεύθυνση πορτοφολιού)", "pool unreachable": "Το pool δεν είναι προσβάσιμο", "no hashing": "Δεν γίνεται hashing", "most shares are rejected": "Τα περισσότερα shares απορρίπτονται", "no accepted shares for a long time": "Δεν υπάρχουν αποδεκτά shares εδώ και ώρα", "SHA self-test failed - mining disabled": "Απέτυχε ο αυτοέλεγχος SHA - η εξόρυξη απενεργοποιήθηκε", "cannot connect to pool": "αδυναμία σύνδεσης στο pool", "pool closed the connection, reconnecting": "το pool έκλεισε τη σύνδεση, επανασύνδεση", "subscribe failed": "αποτυχία subscribe", "rejected, no reason given": "απορρίφθηκε, χωρίς αιτιολογία", "login refused by pool": "σύνδεση απορρίφθηκε από το pool", "invalid job id": "Μη έγκυρο job id", "unauthorized worker": "μη εξουσιοδοτημένος worker", "low difficulty share": "share χαμηλής δυσκολίας", "duplicate share": "διπλό share", "job not found": "η εργασία δεν βρέθηκε", "stale share": "παλιό share", "Miner name must be exactly 4 letters/digits": "Το όνομα miner πρέπει να είναι ακριβώς 4 γράμματα/ψηφία", "Invalid wallet address": "Μη έγκυρη διεύθυνση πορτοφολιού", "Invalid pool URL": "Μη έγκυρη διεύθυνση pool", "Worker: letters/digits/_/- only": "Worker: μόνο γράμματα/ψηφία/_/-", "Suggested difficulty must be a number like 0.0001 (or blank)": "Η προτεινόμενη δυσκολία πρέπει να είναι αριθμός όπως 0.0001 (ή κενό)", "Saved. Rebooting... new address:": "Αποθηκεύτηκε. Επανεκκίνηση... νέα διεύθυνση:"};
const MODE_EL=[["software (hardware SHA self-test failed)", "λογισμικό (απέτυχε ο αυτοέλεγχος SHA υλικού)"], ["software (hardware SHA unreliable)", "λογισμικό (το SHA υλικού δεν είναι αξιόπιστο)"], ["hardware SHA off", "το SHA υλικού είναι ανενεργό"], ["chip rev", "έκδοση chip"], ["hardware SHA", "SHA υλικού"], ["software (CPU)", "λογισμικό (CPU)"], ["software", "λογισμικό"], ["(fast, +2 margin)", "(γρήγορο, +2 περιθώριο)"], ["(fast, wide margin)", "(γρήγορο, ευρύ περιθώριο)"], ["(fast)", "(γρήγορο)"], ["(polled)", "(polling)"]];
const t=s=>(lang&&s!=null&&EL[s]!==undefined)?EL[s]:s;
const terr=e=>{if(!lang||!e)return e;if(EL[e]!==undefined)return EL[e];const l=e.toLowerCase();if(EL[l]!==undefined)return EL[l];
if(e.indexOf('login refused by pool')==0)return EL['login refused by pool']+e.slice(21);if(e.indexOf('Saved. Rebooting... new address:')==0)return EL['Saved. Rebooting... new address:']+e.slice(32);return e};
const fmtEv=e=>{const k=e.indexOf(': ');if(e[0]=='!')return '⚠ '+t(e.slice(2));if(k>0){const p=e.slice(0,k),r=e.slice(k+2);if(p=='start')return t('start')+': '+trst(r);if(p=='restart')return t('restart')+': '+terr(r);if(p=='ok')return '✔ '+t(r);if(p=='power')return t('power')+': '+t(r.replace(/ \d$/,''))+r.slice(r.length-2)}return e};
const loadLog=async()=>{try{const r=await(await fetch('/api/log')).json(),el=$('evlog');el.textContent='';
r.ev.forEach(e=>{const d=document.createElement('div');d.textContent='#'+e.b+' +'+dur(e.u)+'  '+fmtEv(e.t);el.appendChild(d)});if(!r.ev.length)el.textContent='-'}catch(e){}};
const trst=e=>{const i=e.indexOf(' (self-heal');return i<0?terr(e):terr(e.slice(0,i))+e.slice(i)};
const tmode=m=>{if(!lang||!m)return m;MODE_EL.forEach(p=>{m=m.split(p[0]).join(p[1])});return m};
const applyLang=()=>{document.documentElement.lang=lang?'el':'en';
document.querySelectorAll('[data-t]').forEach(e=>{if(e.dataset.en===undefined)e.dataset.en=e.textContent;e.textContent=t(e.dataset.en)});
document.querySelectorAll('[data-tp]').forEach(e=>{e.placeholder=t(e.dataset.tp)})};
const si=(n)=>{const u=['','K','M','G','T','P'];let i=0;while(n>=1000&&i<5){n/=1000;i++}return n.toFixed(n<10?2:1)+u[i]};
const dur=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60),u=lang?['ημ','ω','λ']:['d','h','m'];return(d?d+u[0]+' ':'')+h+u[1]+' '+m+u[2]};
const MAXH=151,COL=['#4fc3f7','#d77bff','#7ee081','#ffd166'];let lastA=-1,lastR=-1,cur=-1;
const addSample=s=>{const a=s.acc||0,r=s.rej||0;hist.push({hr:s.hr||0,c:(s.cores||[]).slice(0,4),da:(lastA>=0&&a>lastA)?1:0,dr:(lastR>=0&&r>lastR)?1:0});lastA=a;lastR=r;if(hist.length>MAXH)hist.shift();draw()};
const nice=v=>{if(!(v>0))return 1;const e=Math.pow(10,Math.floor(Math.log10(v))),f=v/e;return(f<=1?1:f<=2?2:f<=2.5?2.5:f<=5?5:10)*e};
const tfmt=s=>{s=Math.round(s);if(s<=0)return t('now');return s>=60?'-'+(s/60)+'m':'-'+s+'s'};
const draw=()=>{const c=$('ch'),dpr=window.devicePixelRatio||1,W=c.clientWidth||700,H=c.clientHeight||250;
if(c.width!==Math.round(W*dpr)||c.height!==Math.round(H*dpr)){c.width=Math.round(W*dpr);c.height=Math.round(H*dpr)}
const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,W,H);
const L=46,R=10,T=40,B=20,pw=W-L-R,ph=H-T-B,n=hist.length,nc=n?hist[n-1].c.length:0,dx=pw/(MAXH-1);
let hi=1;hist.forEach(p=>{hi=Math.max(hi,p.hr);p.c.forEach(v=>{hi=Math.max(hi,v)})});const mx=nice(hi*1.08);
const X=i=>L+pw-(n-1-i)*dx,Y=v=>T+ph-Math.min(v,mx)/mx*ph;
g.fillStyle='#050a0c';g.fillRect(L,T,pw,ph);
const vg=g.createRadialGradient(L+pw/2,T+ph/2,ph*.2,L+pw/2,T+ph/2,pw*.65);vg.addColorStop(0,'rgba(40,90,70,.10)');vg.addColorStop(1,'rgba(0,0,0,.35)');g.fillStyle=vg;g.fillRect(L,T,pw,ph);
g.lineWidth=1;g.strokeStyle='rgba(110,255,200,.09)';g.beginPath();
for(let k=1;k<10;k++){const x=Math.round(L+pw*k/10)+.5;g.moveTo(x,T);g.lineTo(x,T+ph)}
for(let k=1;k<5;k++){const y=Math.round(T+ph*k/5)+.5;g.moveTo(L,y);g.lineTo(L+pw,y)}g.stroke();
g.strokeStyle='rgba(110,255,200,.22)';g.beginPath();
const cx=Math.round(L+pw/2)+.5,cy=Math.round(T+ph/2)+.5;
for(let k=0;k<=50;k++){const x=L+pw*k/50;g.moveTo(x,cy-(k%5?2:4));g.lineTo(x,cy+(k%5?2:4))}
for(let k=0;k<=25;k++){const y=T+ph*k/25;g.moveTo(cx-(k%5?2:4),y);g.lineTo(cx+(k%5?2:4),y)}g.stroke();
g.strokeStyle='rgba(110,255,200,.35)';g.strokeRect(L+.5,T+.5,pw-1,ph-1);
g.font='10px system-ui,sans-serif';g.fillStyle='#8b92a5';g.textAlign='right';g.textBaseline='middle';
for(let k=0;k<=5;k++)g.fillText(si(mx*k/5),L-6,T+ph-ph*k/5);
g.textBaseline='top';const span=(MAXH-1)*2;
for(let k=0;k<=5;k++){g.textAlign=k==0?'left':(k==5?'right':'center');g.fillText(tfmt(span*(5-k)/5),L+pw*k/5,T+ph+5)}
if(n<2){g.textAlign='center';g.textBaseline='middle';g.fillStyle='#8b92a5';g.fillText('…',L+pw/2,T+ph/2);return}
g.save();g.beginPath();g.rect(L,T,pw,ph);g.clip();
hist.forEach((p,i)=>{if(p.da||p.dr){g.strokeStyle=p.dr?'rgba(255,92,92,.45)':'rgba(62,207,142,.35)';g.setLineDash([2,4]);g.beginPath();g.moveTo(X(i)+.5,T);g.lineTo(X(i)+.5,T+ph);g.stroke();g.setLineDash([]);
g.fillStyle=p.dr?'#ff5c5c':'#3ecf8e';g.beginPath();g.moveTo(X(i),T+2);g.lineTo(X(i)-4,T+10);g.lineTo(X(i)+4,T+10);g.closePath();g.fill()}});
let sum=0,mn=1e18,mxv=0;hist.forEach(p=>{sum+=p.hr;mn=Math.min(mn,p.hr);mxv=Math.max(mxv,p.hr)});const avg=sum/n;
g.setLineDash([6,4]);g.strokeStyle='rgba(255,255,255,.28)';g.beginPath();g.moveTo(L,Y(avg));g.lineTo(L+pw,Y(avg));g.stroke();g.setLineDash([]);
const trace=(f)=>{g.beginPath();hist.forEach((p,i)=>{const x=X(i),y=Y(f(p));i?g.lineTo(x,y):g.moveTo(x,y)})};
if(nc>1)for(let k=0;k<nc;k++){trace(p=>p.c[k]||0);g.lineWidth=1.3;g.strokeStyle=COL[k];g.globalAlpha=.8;g.stroke();g.globalAlpha=1}
trace(p=>p.hr);g.lineTo(X(n-1),T+ph);g.lineTo(X(0),T+ph);g.closePath();const fg=g.createLinearGradient(0,T,0,T+ph);fg.addColorStop(0,'rgba(247,147,26,.30)');fg.addColorStop(1,'rgba(247,147,26,0)');g.fillStyle=fg;g.fill();
g.lineJoin='round';trace(p=>p.hr);g.shadowColor='#f7931a';g.shadowBlur=10;g.lineWidth=2.2;g.strokeStyle='#ffb347';g.stroke();g.shadowBlur=0;
const lx=X(n-1),ly=Y(hist[n-1].hr);g.fillStyle='#fff';g.shadowColor='#f7931a';g.shadowBlur=12;g.beginPath();g.arc(lx,ly,3,0,7);g.fill();g.shadowBlur=0;
if(cur>=0&&cur<n){const p=hist[cur],x=X(cur);g.strokeStyle='rgba(255,255,255,.45)';g.beginPath();g.moveTo(x+.5,T);g.lineTo(x+.5,T+ph);g.stroke();
const rows=[[t('Total'),p.hr,'#ffb347']].concat(p.c.length>1?p.c.map((v,k)=>[t('Core')+' '+k,v,COL[k]]):[]);
const bw=118,bh=8+rows.length*14+14,bx=Math.min(Math.max(x+8,L+2),L+pw-bw-2),by=T+14;
g.fillStyle='rgba(8,14,16,.92)';g.fillRect(bx,by,bw,bh);g.strokeStyle='rgba(110,255,200,.35)';g.strokeRect(bx+.5,by+.5,bw-1,bh-1);
g.textAlign='left';g.textBaseline='top';g.fillStyle='#8b92a5';g.fillText(tfmt((n-1-cur)*2),bx+6,by+5);
rows.forEach((r,j)=>{g.fillStyle=r[2];g.fillText(r[0]+'  '+si(r[1])+'H/s',bx+6,by+19+j*14)})}
g.restore();
g.textBaseline='middle';g.textAlign='left';g.font='11px system-ui,sans-serif';let px=L;
[[t('NOW'),hist[n-1].hr,'#ffb347'],[t('AVG'),avg,'#cfd3dc'],[t('MAX'),mxv,'#3ecf8e'],[t('MIN'),mn,'#8b92a5']].forEach(r=>{g.fillStyle='#8b92a5';g.fillText(r[0],px,11);const w=g.measureText(r[0]+' ').width;g.fillStyle=r[2];g.font='bold 11px system-ui,sans-serif';g.fillText(si(r[1]),px+w,11);px+=w+g.measureText(si(r[1])).width+14;g.font='11px system-ui,sans-serif'});
px=L;g.font='10px system-ui,sans-serif';const lg=[[t('Total'),'#ffb347']].concat(nc>1?hist[n-1].c.map((v,k)=>[t('Core')+' '+k,COL[k]]):[]);
lg.forEach(r=>{g.fillStyle=r[1];g.fillRect(px,26,10,3);g.fillStyle='#8b92a5';g.fillText(r[0],px+14,28);px+=14+g.measureText(r[0]).width+12});
[['#3ecf8e',t('accepted')],['#ff5c5c',t('rejected')]].forEach(r=>{const w=g.measureText(r[1]).width,fit=px+12+w<W-4;g.fillStyle=r[0];g.beginPath();g.moveTo(px+4,24);g.lineTo(px,31);g.lineTo(px+8,31);g.closePath();g.fill();if(fit){g.fillStyle='#8b92a5';g.fillText(r[1],px+12,28);px+=12+w+10}else px+=14});
};
const scopePos=e=>{const c=$('ch'),r=c.getBoundingClientRect(),p=e.touches?e.touches[0]:e,pw=r.width-56,dx=pw/(MAXH-1),x=p.clientX-r.left-46,n=hist.length;cur=n-1-Math.round((pw-x)/dx);if(cur<0||cur>=n||x<0||x>pw)cur=-1;draw()};
$('ch').onmousemove=scopePos;$('ch').ontouchmove=scopePos;$('ch').ontouchstart=scopePos;$('ch').onmouseleave=()=>{cur=-1;draw()};$('ch').ontouchend=()=>{cur=-1;draw()};window.onresize=()=>draw();
const fmtd=n=>n>=1?si(n):(n>0?n.toPrecision(2):'-');
const eta=(d,h)=>{if(!h||!d)return'-';const t0=d*4294967296/h,u=lang?['δλ','λ','ω','ημέρες']:['s','min','h','days'];if(t0<90)return Math.round(t0)+' '+u[0];if(t0<5400)return Math.round(t0/60)+' '+u[1];if(t0<172800)return(t0/3600).toFixed(1)+' '+u[2];return Math.round(t0/86400)+' '+u[3]};
const tick=async()=>{try{const s=await(await fetch('/api/status')).json();
$('hr').textContent=si(s.hr)+'H/s';$('sh').textContent=s.acc+' / '+s.rej;$('best').textContent=si(s.best);
$('pd').textContent=fmtd(s.pdiff);$('eta').textContent=eta(s.pdiff,s.hr);$('up').textContent=dur(s.up);$('pool').textContent=s.pool;
$('cores').textContent=s.cores.map(x=>si(x)).join(' + ');$('rssi').textContent=s.rssi+' dBm';
$('sys').textContent=s.temp.toFixed(0)+'°C / '+Math.round(s.heap/1024)+'K';
$('host').textContent=s.host;$('ip').textContent=s.ip;$('mac').textContent=s.mac;document.title='CRSminer '+s.host.replace('crsminer-','').replace('.local','');
$('stage').textContent=t('Status:')+' '+t(s.stage||'-')+'  |  SHA: '+tmode(s.mode||'-')+(s.diag?'  |  ⚠ '+t(s.diag):'')+(s.err?'  |  '+terr(s.err):'');$('stage').className=(s.err||s.diag)?'bad':'';
$('d_login').textContent=t('login')+': '+(s.auth||'-');$('d_sent').textContent=t('sent:')+' '+(s.sub||t('none yet'));$('d_rep').textContent=t('reply:')+' '+(s.rep||'-');$('d_rst').textContent=t('last start:')+' '+trst(s.rst||'-')+(s.gen?'  ·  '+t('gentle power mode')+' '+s.gen:'');
const c=$('conn');c.textContent=s.ok?t('● connected'):t('● pool offline');c.className=s.ok?'ok':'bad';
addSample(s)}catch(e){$('conn').textContent=t('● offline');$('conn').className='bad'}};
const load=async()=>{const c=await(await fetch('/api/config')).json();$('c_name').value=c.name;$('c_pool').value=c.pool;$('c_addr').value=c.addr;$('c_worker').value=c.worker;$('c_led').value=String(c.ledmode||0);$('c_ledpin').value=(c.ledpin===undefined?2:c.ledpin);$('c_lang').value=String(c.lang||0);$('c_sugg').value=c.sugg||'';lang=c.lang==1?1:0;applyLang()};
$('c_lang').onchange=()=>{lang=$('c_lang').value=='1'?1:0;applyLang()};
$('f').onsubmit=async e=>{e.preventDefault();const b=new URLSearchParams({name:$('c_name').value,pool:$('c_pool').value,addr:$('c_addr').value,worker:$('c_worker').value,pass:$('c_pass').value,ledmode:$('c_led').value,ledpin:$('c_ledpin').value,lang:$('c_lang').value,sugg:$('c_sugg').value});
const r=await fetch('/api/config',{method:'POST',body:b});$('msg').textContent=terr(await r.text());$('msg').className=r.ok?'ok':'bad'};
$('rw').onclick=async()=>{if(confirm(t('Erase WiFi credentials and reboot into setup mode?'))){await fetch('/api/resetwifi',{method:'POST'});$('msg').textContent=t('Rebooting into setup AP...')}};
load();tick();loadLog();setInterval(tick,2000);setInterval(loadLog,30000);
</script></body></html>)rawliteral";


static const char LICENSE_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CRSminer licence</title>
<style>body{margin:0;background:#0f1115;color:#e8eaf0;font:15px system-ui,sans-serif;padding:16px;max-width:760px;margin:auto;line-height:1.5}
h1{font-size:20px}h2{font-size:15px;color:#8b92a5;margin-top:22px}pre{white-space:pre-wrap;background:#181b22;border-radius:10px;padding:12px;font-size:13px}a{color:#f7931a}</style></head><body>
<h1>CRSminer &mdash; MIT License</h1>
<p>Copyright (c) 2026 HexOhm</p>
<pre>Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.</pre>
<h2>Third-party credits</h2>
<p>The hardware SHA-256 mining loops and their known-answer self-test are adapted from
<b>NerdMiner Modded</b> by HenrysCat, <b>NerdMiner V2</b> by BitMaker-hub and
<b>nerdminer-axehub</b> by dwespl, all under the MIT License:</p>
<pre>Copyright (c) 2023 Bitmaker
Copyright (c) 2026 HenrysCat (NerdMiner Modded modifications)
MIT License - permission notice as above.</pre>
<p>The libraries WiFiManager (tzapu), ArduinoJson (Benoit Blanchon), the Arduino-ESP32 core and, on the CYD
version, LovyanGFX are used under their own licences.</p>
<p lang="el">Άδεια MIT. Το λογισμικό παρέχεται «ως έχει», χωρίς καμία εγγύηση. Οι πιστώσεις τρίτων ισχύουν όπως παραπάνω.</p>
<p><a href="/">&larr; back / πίσω</a></p></body></html>)rawliteral";

// ------------------------------------------------------------ config
static Cfg cfg;
static Preferences prefs;
static volatile bool shouldSave = false;

// GPIO the user may choose for the status LED: -1 (none) or an output-capable, non-reserved pin
static bool validLedPin(int p) {
  if (p == -1) return true;
  if (p < 0 || p > 48) return false;
  if (CRS_RESERVED_PIN(p)) return false;
#if defined(CONFIG_IDF_TARGET_ESP32)
  if (p > 33 || (p >= 6 && p <= 11) || p == 1 || p == 3) return false;  // input-only, SPI flash, UART0
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  if ((p >= 26 && p <= 32) || p == 19 || p == 20 || p == 43 || p == 44) return false;  // flash, USB, UART0
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  if (p > 21 || (p >= 12 && p <= 17) || p == 18 || p == 19 || p == 20 || p == 21) return false;  // flash, USB, UART0
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  if (p > 23 || p == 12 || p == 13 || p == 16 || p == 17) return false;  // flash, USB, UART0
#endif
  return true;
}
static bool parsePin(const char *t, int &out) {
  while (*t == ' ') t++;
  bool neg = (*t == '-');
  if (neg) t++;
  if (!*t) return false;
  int v = 0, n = 0;
  for (; *t >= '0' && *t <= '9'; t++, n++) { v = v * 10 + (*t - '0'); if (v > 999) return false; }
  while (*t == ' ') t++;
  if (n == 0 || *t) return false;
  out = neg ? -v : v;
  return true;
}
static int defaultLedPin() { return validLedPin(BLUE_LED_PIN) ? BLUE_LED_PIN : -1; }
static void loadCfg() {
  prefs.begin("crsminer", true);
  cfg.name = prefs.getString("name", cfg.name);
  cfg.pool = prefs.getString("pool", cfg.pool);
  cfg.addr = prefs.getString("addr", cfg.addr);
  cfg.worker = prefs.getString("worker", cfg.worker);
  cfg.pass = prefs.getString("pass", cfg.pass);
  cfg.ledMode = prefs.isKey("ledmode") ? prefs.getUChar("ledmode", 0) : (prefs.getBool("ledoff", false) ? 1 : 0);
  if (cfg.ledMode > 2) cfg.ledMode = 0;
  cfg.lang = prefs.getUChar("lang", 0) == 1 ? 1 : 0;
  cfg.ledPin = prefs.getInt("ledpin", defaultLedPin());
  if (!validLedPin(cfg.ledPin)) cfg.ledPin = defaultLedPin();
  cfg.sugg = prefs.getString("sugg", cfg.sugg);
  prefs.end();
}
static void saveCfg() {
  prefs.begin("crsminer", false);
  prefs.putString("name", cfg.name);
  prefs.putString("pool", cfg.pool);
  prefs.putString("addr", cfg.addr);
  prefs.putString("worker", cfg.worker);
  prefs.putString("pass", cfg.pass);
  prefs.putUChar("ledmode", cfg.ledMode);
  prefs.putUChar("lang", cfg.lang);
  prefs.putInt("ledpin", cfg.ledPin);
  prefs.putString("sugg", cfg.sugg);
  prefs.end();
}
static String hostName = "crsminer";
static bool validName(const String &n) {
  if (n.length() != 4) return false;
  for (size_t i = 0; i < 4; i++)
    if (!isalnum((unsigned char)n[i])) return false;
  return true;
}
static bool validAddr(const String &a) {
  if (a.length() < 26 || a.length() > 90) return false;
  for (size_t i = 0; i < a.length(); i++)
    if (!isalnum((unsigned char)a[i]) && a[i] != ':') return false;  // ':' for bitcoincash: style prefixes
  return true;
}
static bool validSugg(const String &v) {
  if (!v.length()) return true;  // blank = off
  if (v.length() > 12) return false;
  for (size_t i = 0; i < v.length(); i++) {
    char c = v[i];
    if (!isdigit((unsigned char)c) && c != '.' && c != 'e' && c != 'E' && c != '-') return false;
  }
  double d = atof(v.c_str());
  return d > 0 && d <= 1e9;
}
static void parsePool(String url, String &host, uint16_t &port) {
  url.trim();
  int p = url.indexOf("://");
  if (p >= 0) url = url.substring(p + 3);
  while (url.endsWith("/")) url.remove(url.length() - 1);
  int c = url.lastIndexOf(':');
  port = 3333;
  if (c > 0) { port = url.substring(c + 1).toInt(); url = url.substring(0, c); }
  host = url;
}

// ------------------------------------------------------------ LED / quiet mode
static volatile bool ledQuiet = false;  // true = serial status lines are muted
#define LOGF(...) do { if (!ledQuiet) Serial.printf(__VA_ARGS__); } while (0)
static const char *volatile diagMsg = "";  // why the diagnostic LED is steady on ("" = all fine)
static void applyLed() {
  if (cfg.ledMode != 2) ledQuiet = (cfg.ledMode == 1);  // diagnostic mode is driven by ledTask
}

// ------------------------------------------------------------ state
static Job curJob;
static volatile uint32_t jobSeq = 0;
static SemaphoreHandle_t jobMux;
static QueueHandle_t shareQ;
static volatile bool poolReady = false, poolConnected = false, selfOk = false, restartStratum = false;
static volatile double poolDiff = 1.0, bestDiff = 0;
static volatile uint32_t accepted = 0, rejected = 0, jobCount = 0;
static char lastErr[80] = "";
static char lastSub[200] = "";   // last mining.submit sent
static char lastAuth[420] = "";  // authorize/subscribe exchange, raw
static volatile bool authDenied = false;
static volatile uint8_t authStreak = 0;   // consecutive logins refused by the pool (cleared by a good login)
static volatile uint32_t authSince = 0;
static char bootNote[64] = "";            // why the miner (re)started, shown on the dashboard
// Gentle power mode: after brownout resets (supply too weak) the miner lowers its current draw by itself:
// level 1-3 = lower WiFi transmit power + short pauses between hash chunks. Eased after 24 h of stable running.
static uint8_t gentle = 0;
static uint16_t hwGapMs = 0, swGapMs = 0;
static void applyTxPower() {
  if (!gentle) return;
  WiFi.setTxPower(gentle == 1 ? WIFI_POWER_15dBm : gentle == 2 ? WIFI_POWER_11dBm : WIFI_POWER_8_5dBm);
}

// ---- persistent event log: last 10 events (restarts, problems, recoveries) kept in flash.
// Written only when something happens (a handful of times per day at most), never while mining normally.
#define EV_MAX 10
struct EvEntry { uint32_t boot; uint32_t up; char txt[44]; };
static EvEntry evBuf[EV_MAX];  // oldest first
static uint8_t evCount = 0;
static uint32_t bootNo = 0;
static SemaphoreHandle_t evMux = NULL;
static void evLoad() {
  Preferences p;
  if (!p.begin("crsev", false)) return;
  bootNo = p.getUInt("boot", 0) + 1;
  p.putUInt("boot", bootNo);
  if (p.getBytesLength("ev") == sizeof(evBuf)) p.getBytes("ev", evBuf, sizeof(evBuf));
  evCount = p.getUChar("evn", 0);
  if (evCount > EV_MAX) evCount = 0;
  p.end();
}
static void evLog(const char *txt) {
  if (!evMux) return;
  xSemaphoreTake(evMux, portMAX_DELAY);
  if (evCount >= EV_MAX) { memmove(&evBuf[0], &evBuf[1], sizeof(EvEntry) * (EV_MAX - 1)); evCount = EV_MAX - 1; }
  EvEntry &e = evBuf[evCount++];
  e.boot = bootNo; e.up = millis() / 1000; strlcpy(e.txt, txt, sizeof(e.txt));
  Preferences p;
  if (p.begin("crsev", false)) { p.putBytes("ev", evBuf, sizeof(evBuf)); p.putUChar("evn", evCount); p.end(); }
  xSemaphoreGive(evMux);
}

// Self-healing restart: remember the reason across the reboot so the dashboard can show it.
static void crsRestart(const char *why) {
  Serial.printf("SELF-HEAL restart: %s\n", why);
  { char b[48]; snprintf(b, sizeof(b), "restart: %s", why); evLog(b); }
  Preferences p;
  if (p.begin("crsrst", false)) { p.putString("why", why); p.putUInt("n", p.getUInt("n", 0) + 1); p.end(); }
  delay(200);
  ESP.restart();
}
static char lastRep[200] = "";   // last pool reply to a submit (raw)
static const char *volatile stage = "starting";
static int nWorkers = 1;
static struct { volatile uint32_t hashes; uint32_t last; float rate; } wst[MAX_WORKERS];
static volatile float totalRate = 0;
static String poolHost;
static uint16_t poolPort;

// ------------------------------------------------------------ helpers
static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}
static void hex2bytes(const char *s, std::vector<uint8_t> &out) {
  out.clear();
  size_t n = strlen(s) / 2;
  for (size_t i = 0; i < n; i++) out.push_back((hexv(s[2 * i]) << 4) | hexv(s[2 * i + 1]));
}

static void midstateFromHeader(const uint8_t hdr[80], Work &w) {
  uint32_t blk[16];
  memcpy(w.mid, SHA_IV, 32);
  for (int k = 0; k < 16; k++) blk[k] = be32(hdr + 4 * k);
  sha_compress(w.mid, blk);
  w.w0 = be32(hdr + 64); w.w1 = be32(hdr + 68); w.w2 = be32(hdr + 72);
  for (int k = 0; k < 19; k++) w.hdr[k] = be32(hdr + 4 * k);
  w.hdr[19] = 0;
}

static void buildWork(const Job &j, uint64_t en2val, Work &w) {
  uint8_t en2[16] = {0};
  int sz = j.en2Size > 16 ? 16 : j.en2Size;
  for (int k = 0; k < sz && k < 8; k++) en2[k] = (en2val >> (8 * k)) & 0xff;
  std::vector<uint8_t> cb;
  cb.reserve(j.cb1.size() + j.en1.size() + sz + j.cb2.size());
  cb.insert(cb.end(), j.cb1.begin(), j.cb1.end());
  cb.insert(cb.end(), j.en1.begin(), j.en1.end());
  cb.insert(cb.end(), en2, en2 + sz);
  cb.insert(cb.end(), j.cb2.begin(), j.cb2.end());
  uint8_t root[32];
  sha256d(cb.data(), cb.size(), root);
  for (size_t i = 0; i + 32 <= j.branch.size(); i += 32) {
    uint8_t buf[64];
    memcpy(buf, root, 32); memcpy(buf + 32, &j.branch[i], 32);
    sha256d(buf, 64, root);
  }
  uint8_t hdr[80];
  put32le(hdr, j.version);
  memcpy(hdr + 4, j.prev, 32);
  memcpy(hdr + 36, root, 32);
  put32le(hdr + 68, j.ntime);
  put32le(hdr + 72, j.nbits);
  put32le(hdr + 76, 0);
  midstateFromHeader(hdr, w);
  w.ntime = j.ntime;
  strlcpy(w.jobId, j.id.c_str(), sizeof(w.jobId));
  for (int k = 0; k < sz; k++) sprintf(w.en2hex + 2 * k, "%02x", en2[k]);
  w.en2hex[2 * sz] = 0;
}

// ------------------------------------------------------------ workers
static bool handleCandidate(const Work &w, uint32_t nonce, uint64_t t64) {
  uint32_t top, h6;
  hash_full(w, nonce, &top, &h6);
  uint64_t val = ((uint64_t)top << 32) | h6;
  if (val == 0) val = 1;
  double d = DIFF1_TOP64 / (double)val;
  if (d > bestDiff) bestDiff = d;
  if (val <= t64) {
    Share s;
    strlcpy(s.job, w.jobId, sizeof(s.job));
    strlcpy(s.en2, w.en2hex, sizeof(s.en2));
    s.ntime = w.ntime; s.nonce = nonce;
    xQueueSend(shareQ, &s, 0);
  }
  return top < 0x10000;  // true = the hash really has 16 leading zero bits
}

// ------------------------------------------------------------ hardware SHA (classic ESP32)
static volatile int hwLoop = -1;  // index of the active hardware loop, -1 = software only
static int hwId = -1;             // worker that drives the (single) SHA engine
static char hwMode[64] = "software (CPU)";
#define HW_CHUNK 131072UL

#if CRS_HW_SHA
extern "C" {
uint32_t pipelined_hw_mine_classic_v2(volatile uint32_t *sha_base, PlCtx *ctx);
uint32_t pipelined_hw_mine_classic_v2_margin(volatile uint32_t *sha_base, PlCtx *ctx);
uint32_t pipelined_hw_mine_classic_v2_wide(volatile uint32_t *sha_base, PlCtx *ctx);
}

// --- the three timing variants of the pipelined loop (assembly; see credits at the top)
asm(
    ".equ CTX_HDR16, 64\n"
    ".equ CTX_NONCE, 80\n"
    ".equ CTX_BUDGET, 84\n"
    ".equ CTX_RUN, 88\n"
    ".equ CTX_PAD, 92\n"
    ".equ CTX_CHK, 96\n"
    ".equ R_START, 0x90\n"
    ".equ R_CONT, 0x94\n"
    ".equ R_LOAD, 0x98\n"
    "    .macro NOPS n\n"
    "    .rept \\n\n"
    "    nop.n\n"
    "    .endr\n"
    "    .endm\n"
    "    .macro ENGINE_SETTLE\n"
    "    movi.n  a7, 28\n"
    "1:  addi.n  a7, a7, -1\n"
    "    bnez    a7, 1b\n"
    "    .endm\n"
    "    .macro COPY src, dst, wgap\n"
    "    l32i    a7, a3, \\src\n"
    "    s32i.n  a7, a2, \\dst\n"
    "    NOPS    \\wgap\n"
    "    .endm\n"
    "    .macro ZERO dst, wgap\n"
    "    s32i.n  a8, a2, \\dst\n"
    "    nop.n\n"
    "    NOPS    \\wgap\n"
    "    .endm\n"
    "    .macro HDRW k, lpos, wgap\n"
    "    COPY    \\k * 4, \\k * 4, \\wgap\n"
    "    .if \\lpos == \\k + 1\n"
    "    l16ui   a6, a2, 28\n"
    "    .endif\n"
    "    .endm\n"
    "    .macro PL_MINE name, sect, p1, d2, p2, pl1, d3, p3, pl2, lpos, wgap, chk\n"
    "    .pushsection \\sect, \"ax\"\n"
    "    .global \\name\n"
    "    .type   \\name, @function\n"
    "    .balign 4\n"
    "\\name:\n"
    "    entry   a1, 32\n"
    "    l32i    a4, a3, CTX_NONCE\n"
    "    l32i    a5, a3, CTX_BUDGET\n"
    "    movi.n  a8, 0\n"
    "    ENGINE_SETTLE\n"
    "    COPY    0, 0, \\wgap\n"
    "    COPY    4, 4, \\wgap\n"
    "    COPY    8, 8, \\wgap\n"
    "    COPY    12, 12, \\wgap\n"
    "    COPY    16, 16, \\wgap\n"
    "    COPY    20, 20, \\wgap\n"
    "    COPY    24, 24, \\wgap\n"
    "    COPY    28, 28, \\wgap\n"
    "    COPY    32, 32, \\wgap\n"
    "    COPY    36, 36, \\wgap\n"
    "    COPY    40, 40, \\wgap\n"
    "    COPY    44, 44, \\wgap\n"
    "    COPY    48, 48, \\wgap\n"
    "    COPY    52, 52, \\wgap\n"
    "    COPY    56, 56, \\wgap\n"
    "    COPY    60, 60, \\wgap\n"
    "    movi.n  a6, 1\n"
    "    s32i    a6, a2, R_START\n"
    "    j       .Lhead_\\name\n"
    "    .balign 4\n"
    ".Lw2b_\\name:\n"
    "    NOPS    \\p2\n"
    "    s32i    a6, a2, R_LOAD\n"
    "    NOPS    \\pl1\n"
    "    s32i    a6, a2, R_START\n"
    "    NOPS    \\d3\n"
    "    COPY    32, 32, \\wgap\n"
    "    COPY    36, 36, \\wgap\n"
    "    COPY    40, 40, \\wgap\n"
    "    COPY    44, 44, \\wgap\n"
    "    COPY    48, 48, \\wgap\n"
    "    COPY    52, 52, \\wgap\n"
    "    COPY    56, 56, \\wgap\n"
    "    COPY    60, 60, \\wgap\n"
    "    NOPS    \\p3\n"
    "    s32i    a6, a2, R_LOAD\n"
    "    NOPS    \\pl2\n"
    "    HDRW    0, \\lpos, \\wgap\n"
    "    HDRW    1, \\lpos, \\wgap\n"
    "    HDRW    2, \\lpos, \\wgap\n"
    "    HDRW    3, \\lpos, \\wgap\n"
    "    HDRW    4, \\lpos, \\wgap\n"
    "    HDRW    5, \\lpos, \\wgap\n"
    "    HDRW    6, \\lpos, \\wgap\n"
    "    HDRW    7, \\lpos, \\wgap\n"
    "    movi.n  a7, 1\n"
    "    s32i    a7, a2, R_START\n"
    ".Lhead_\\name:\n"
    "    .if \\chk\n"
    "    l32i    a7, a3, CTX_CHK\n"
    "    add.n   a7, a7, a6\n"
    "    s32i    a7, a3, CTX_CHK\n"
    "    .else\n"
    "    NOPS    5\n"
    "    .endif\n"
    "    beqz    a6, .Lhit_\\name\n"
    "    beqz    a5, .Lstop_\\name\n"
    "    l32i    a7, a3, CTX_RUN\n"
    "    beqz    a7, .Lstop_\\name\n"
    "    addi.n  a5, a5, -1\n"
    "    movi.n  a6, 1\n"
    "    COPY    CTX_HDR16, 0, \\wgap\n"
    "    COPY    CTX_HDR16 + 4, 4, \\wgap\n"
    "    COPY    CTX_HDR16 + 8, 8, \\wgap\n"
    "    l32i    a7, a3, CTX_PAD\n"
    "    s32i.n  a4, a2, 12\n"
    "    addi.n  a4, a4, 1\n"
    "    NOPS    \\wgap\n"
    "    s32i.n  a7, a2, 16\n"
    "    nop.n\n"
    "    NOPS    \\wgap\n"
    "    ZERO    20, \\wgap\n"
    "    ZERO    24, \\wgap\n"
    "    ZERO    28, \\wgap\n"
    "    ZERO    32, \\wgap\n"
    "    ZERO    36, \\wgap\n"
    "    ZERO    40, \\wgap\n"
    "    ZERO    44, \\wgap\n"
    "    ZERO    48, \\wgap\n"
    "    ZERO    52, \\wgap\n"
    "    ZERO    56, \\wgap\n"
    "    movi    a7, 640\n"
    "    s32i.n  a7, a2, 60\n"
    "    NOPS    \\p1\n"
    "    s32i    a6, a2, R_CONT\n"
    "    NOPS    \\d2\n"
    "    COPY    CTX_PAD, 32, \\wgap\n"
    "    movi    a7, 256\n"
    "    s32i.n  a7, a2, 60\n"
    "    j       .Lw2b_\\name\n"
    ".Lhit_\\name:\n"
    "    movi.n  a6, 1\n"
    "    j       .Lout_\\name\n"
    ".Lstop_\\name:\n"
    "    movi.n  a6, 0\n"
    ".Lout_\\name:\n"
    "    ENGINE_SETTLE\n"
    "    s32i    a4, a3, CTX_NONCE\n"
    "    s32i    a5, a3, CTX_BUDGET\n"
    "    mov.n   a2, a6\n"
    "    retw.n\n"
    "    .size   \\name, . - \\name\n"
    "    .popsection\n"
    "    .endm\n"
    "    PL_MINE pipelined_hw_mine_classic_v2,        .iram1.plmine2,   4, 16, 40, 10, 14, 27, 10, 2, 1, 0\n"
    "    PL_MINE pipelined_hw_mine_classic_v2_margin, .iram1.plmine2m,  6, 16, 42, 12, 14, 29, 12, 2, 1, 0\n"
    "    PL_MINE pipelined_hw_mine_classic_v2_wide,   .iram1.plmine2w, 18, 16, 54, 24, 14, 41, 24, 2, 1, 0\n"
);


static void crsShaLock() {
#if CRS_SHA_LOCK
  esp_sha_lock_engine(SHA2_256);
#endif
}
static void crsShaUnlock() {
#if CRS_SHA_LOCK
  esp_sha_unlock_engine(SHA2_256);
#endif
}
// bring the engine to a clean idle state
static void crsShaPrep() {
#if CRS_HAVE_PERIPH
  periph_module_reset(PERIPH_SHA_MODULE);
#else
  static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&m);
  DPORT_REG_SET_BIT(DPORT_PERI_CLK_EN_REG, DPORT_PERI_EN_SHA);
  DPORT_REG_SET_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA);
  DPORT_REG_CLR_BIT(DPORT_PERI_RST_EN_REG, DPORT_PERI_EN_SHA | DPORT_PERI_EN_SECUREBOOT);
  portEXIT_CRITICAL(&m);
#endif
}

// --- last-resort loop: the original BUSY-polling pipelined loop (~27% slower)
static bool IRAM_ATTR plLegacyAsm(volatile uint32_t *sha_base, const uint32_t *header_swapped, uint32_t *nonce_inout,
                                  volatile uint32_t *hash_count_low, volatile bool *mining_flag, uint32_t iter_budget) {
  crsShaPrep();
  __asm__ __volatile__(
      "l32i.n   a2,  %[nonce], 0  \n"
      "addi     a5,  %[sb], 0x90  \n"
      "movi.n   a8,  0            \n"
      "or       a6,  %[budget], %[budget] \n"
      "l32i.n   a7,  %[hcnt], 0   \n"
      "ml_start:                  \n"
      "l32i.n   a3,  %[in],  0    \n"     "l32i.n   a4,  %[in],  4    \n"
      "s32i.n   a3,  %[sb],  0    \n"     "s32i.n   a4,  %[sb],  4    \n"
      "l32i.n   a3,  %[in],  8    \n"     "l32i.n   a4,  %[in], 12    \n"
      "s32i.n   a3,  %[sb],  8    \n"     "s32i.n   a4,  %[sb], 12    \n"
      "l32i.n   a3,  %[in], 16    \n"     "l32i.n   a4,  %[in], 20    \n"
      "s32i.n   a3,  %[sb], 16    \n"     "s32i.n   a4,  %[sb], 20    \n"
      "l32i.n   a3,  %[in], 24    \n"     "l32i.n   a4,  %[in], 28    \n"
      "s32i.n   a3,  %[sb], 24    \n"     "s32i.n   a4,  %[sb], 28    \n"
      "l32i.n   a3,  %[in], 32    \n"     "l32i.n   a4,  %[in], 36    \n"
      "s32i.n   a3,  %[sb], 32    \n"     "s32i.n   a4,  %[sb], 36    \n"
      "l32i.n   a3,  %[in], 40    \n"     "l32i.n   a4,  %[in], 44    \n"
      "s32i.n   a3,  %[sb], 40    \n"     "s32i.n   a4,  %[sb], 44    \n"
      "l32i.n   a3,  %[in], 48    \n"     "l32i.n   a4,  %[in], 52    \n"
      "s32i.n   a3,  %[sb], 48    \n"     "s32i.n   a4,  %[sb], 52    \n"
      "l32i.n   a3,  %[in], 56    \n"     "l32i.n   a4,  %[in], 60    \n"
      "s32i.n   a3,  %[sb], 56    \n"     "s32i.n   a4,  %[sb], 60    \n"
      "movi.n   a3, 1             \n"
      "s32i.n   a3, a5, 0         \n"
      "memw                       \n"
      "l32i     a3,  %[in], 64    \n"     "s32i.n   a3,  %[sb],  0    \n"
      "l32i     a3,  %[in], 68    \n"     "s32i.n   a3,  %[sb],  4    \n"
      "l32i     a3,  %[in], 72    \n"     "s32i.n   a3,  %[sb],  8    \n"
      "s32i.n   a2,  %[sb], 12    \n"
      "movi.n   a3, 1             \n"
      "slli     a3, a3, 31        \n"
      "s32i.n   a3, %[sb], 16     \n"
      "s32i.n   a8,  %[sb], 20    \n"
      "s32i.n   a8,  %[sb], 24    \n"
      "s32i.n   a8,  %[sb], 28    \n"
      "s32i.n   a8,  %[sb], 32    \n"
      "s32i.n   a8,  %[sb], 36    \n"
      "s32i.n   a8,  %[sb], 40    \n"
      "s32i.n   a8,  %[sb], 44    \n"
      "s32i.n   a8,  %[sb], 48    \n"
      "s32i.n   a8,  %[sb], 52    \n"
      "s32i.n   a8,  %[sb], 56    \n"
      "movi     a3, 640           \n"
      "s32i.n   a3, %[sb], 60     \n"
      "ml_w1:                     \n"
      "l32i.n   a3, a5, 12        \n"
      "bnez.n   a3, ml_w1         \n"
      "movi.n   a3, 1             \n"
      "s32i.n   a3, a5, 4         \n"
      "memw                       \n"
      "ml_w2:                     \n"
      "l32i.n   a3, a5, 12        \n"
      "bnez.n   a3, ml_w2         \n"
      "movi.n   a3, 1             \n"
      "s32i.n   a3, a5, 8         \n"
      "memw                       \n"
      "addi.n   a2, a2, 1         \n"
      "ml_w3:                     \n"
      "l32i.n   a3, a5, 12        \n"
      "bnez.n   a3, ml_w3         \n"
      "movi.n   a3, 1             \n"
      "slli     a3, a3, 31        \n"
      "s32i.n   a3, %[sb], 32     \n"
      "movi     a3, 256           \n"
      "s32i.n   a3, %[sb], 60     \n"
      "movi.n   a3, 1             \n"
      "s32i.n   a3, a5, 0         \n"
      "memw                       \n"
      "addi.n   a7, a7, 1         \n"
      "addi     a6, a6, -1        \n"
      "l8ui     a4, %[flag], 0    \n"
      "ml_w4:                     \n"
      "l32i.n   a3, a5, 12        \n"
      "bnez.n   a3, ml_w4         \n"
      "movi.n   a3, 1             \n"
      "s32i.n   a3, a5, 8         \n"
      "memw                       \n"
      "ml_w5:                     \n"
      "l32i.n   a3, a5, 12        \n"
      "bnez.n   a3, ml_w5         \n"
      "bnez.n   a6, ml_budget_ok  \n"
      "s8i      a8, %[flag], 0    \n"
      "j        ml_end            \n"
      "ml_budget_ok:              \n"
      "beqz.n   a4, ml_end        \n"
      "l16ui    a3, %[sb], 28     \n"
      "beqz.n   a3, ml_end        \n"
      "j ml_start                 \n"
      "ml_end:                    \n"
      "s32i.n   a2, %[nonce], 0   \n"
      "s32i.n   a7, %[hcnt], 0    \n"
      :
      : [sb] "r"(sha_base), [in] "r"(header_swapped), [hcnt] "r"(hash_count_low), [nonce] "r"(nonce_inout),
        [flag] "r"(mining_flag), [budget] "r"(iter_budget)
      : "a2", "a3", "a4", "a5", "a6", "a7", "a8", "memory");
  return *mining_flag;
}
static uint32_t IRAM_ATTR plLegacy(volatile uint32_t *sb, PlCtx *c) {
  uint32_t done = 0;
  // the loop polls a bool and clears it itself when its budget is spent; run's low byte serves as that bool
  bool hit = plLegacyAsm(sb, c->hdr, &c->nonce, &done, (volatile bool *)&c->run, c->budget);
  c->budget -= done;
  if (hit) crsShaPrep();
  else if (c->budget == 0) c->run = 1;
  return hit ? 1 : 0;
}

static const struct { PlFn fn; const char *name; } plLoops[] = {
    {pipelined_hw_mine_classic_v2, "hardware SHA (fast)"},
    {pipelined_hw_mine_classic_v2_margin, "hardware SHA (fast, +2 margin)"},
    {pipelined_hw_mine_classic_v2_wide, "hardware SHA (fast, wide margin)"},
    {plLegacy, "hardware SHA (polled)"},
};
#define PL_COUNT ((int)(sizeof(plLoops) / sizeof(plLoops[0])))

// Known-answer test. Header word i = 0x9E3779B9*(i+1); the only candidates (low 16 bits of the
// last digest word zero) among the 262144 nonces from 0x1000 on are listed below (verified
// against Python hashlib). Then the Bitcoin genesis block must be found among 300 nonces.
static bool plSelfTest(PlFn fn) {
  static const uint32_t expected[] = {0xa94c, 0xe496, 0x1f476, 0x302f0, 0x35af9, 0x3cf5c};
  const uint32_t first = 0x1000;
  static PlCtx c;
  for (uint32_t i = 0; i < 19; i++) c.hdr[i] = 0x9E3779B9u * (i + 1);
  c.hdr[19] = 0;
  c.nonce = first; c.budget = 262144; c.pad = 0x80000000u; c.run = 1;
  crsShaPrep();
  size_t found = 0;
  while (c.budget > 0 && fn((volatile uint32_t *)SHA_TEXT_BASE, &c)) {
    if (found >= 6 || c.nonce - 1 - first != expected[found]) return false;
    found++;
  }
  if (found != 6 || c.budget != 0) return false;
  // genesis block
  std::vector<uint8_t> h;
  hex2bytes("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c", h);
  Work gw; midstateFromHeader(h.data(), gw);
  const uint32_t N = 2083236893UL;
  memcpy(c.hdr, gw.hdr, sizeof(c.hdr));
  c.nonce = __builtin_bswap32(N) - 100; c.budget = 300; c.run = 1;
  crsShaPrep();
  bool ok = false;
  while (c.budget > 0 && fn((volatile uint32_t *)SHA_TEXT_BASE, &c))
    if (__builtin_bswap32(c.nonce - 1) == N) ok = true;
  return ok;
}

static void hwSelect() {
  uint32_t rev = ESP.getChipRevision();
  uint32_t major = rev >= 100 ? rev / 100 : rev;
#ifndef CRS_FORCE_HW_SHA
  if (major < 3) {
    snprintf(hwMode, sizeof(hwMode), "software (chip rev %u: hardware SHA off)", (unsigned)major);
    return;
  }
#endif
  crsShaLock();
  int n = 0;
  for (; n < PL_COUNT; n++) {
    bool pass = false;
    for (int t = 0; t < 3 && !pass; t++) pass = plSelfTest(plLoops[n].fn);
    if (pass) break;
    Serial.printf("HW SHA loop '%s' failed its self-test\n", plLoops[n].name);
  }
  crsShaUnlock();
  if (n < PL_COUNT) {
    hwLoop = n;
    strlcpy(hwMode, plLoops[n].name, sizeof(hwMode));
  } else {
    strlcpy(hwMode, "software (hardware SHA self-test failed)", sizeof(hwMode));
  }
}

// every candidate is re-hashed in software; unconfirmed ones mean the loop gets hashes wrong
static void plHealth(bool confirmed) {
  static uint32_t hits = 0, bad = 0;
  hits++;
  if (!confirmed) bad++;
  if (hits < 256) return;
  if (bad >= 8) {
    if (hwLoop + 1 < PL_COUNT) {
      hwLoop = hwLoop + 1;
      strlcpy(hwMode, plLoops[hwLoop].name, sizeof(hwMode));
    } else {
      hwLoop = -1;
      strlcpy(hwMode, "software (hardware SHA unreliable)", sizeof(hwMode));
    }
    Serial.printf("HW SHA: %u of %u candidates unconfirmed, now: %s\n", (unsigned)bad, (unsigned)hits, hwMode);
  }
  hits = bad = 0;
}

// one chunk of the hardware search; cur = byte-swapped nonce cursor. Returns nonces tried.
static uint32_t hwScan(const Work &w, uint64_t t64, uint32_t &cur) {
  static PlCtx c;
  int lp = hwLoop;
  if (lp < 0) return 0;
  memcpy(c.hdr, w.hdr, sizeof(c.hdr));
  c.nonce = cur; c.budget = HW_CHUNK; c.pad = 0x80000000u; c.run = 1;
  crsShaLock();
  crsShaPrep();
  while (c.run && c.budget > 0) {
    if (!plLoops[lp].fn((volatile uint32_t *)SHA_TEXT_BASE, &c)) break;
    uint32_t cand = __builtin_bswap32(c.nonce - 1);
    plHealth(handleCandidate(w, cand, t64));
    if (hwLoop != lp) break;  // switched to a safer loop: restart the chunk loop cleanly
  }
  crsShaUnlock();
  cur = c.nonce;
  return HW_CHUNK - c.budget;
}
#else
static void hwSelect() { strlcpy(hwMode, "software (CPU)", sizeof(hwMode)); }
static uint32_t hwScan(const Work &, uint64_t, uint32_t &) { return 0; }
#endif

static void workerTask(void *arg) {
  int id = (int)(intptr_t)arg;
  uint32_t seq = 0, nonce = 0;
  uint64_t end = 0, en2ctr = id, hwDone = 0;
  uint32_t hwCur = 0;
  uint32_t span = (uint32_t)(0x100000000ULL / nWorkers);
  Work w;
  Job j;
  bool have = false;
  for (;;) {
    if (!selfOk || !poolReady) { vTaskDelay(100); have = false; seq = 0; continue; }
    if (seq != jobSeq) {
      xSemaphoreTake(jobMux, portMAX_DELAY);
      j = curJob; seq = jobSeq;
      xSemaphoreGive(jobMux);
      en2ctr = id;
      buildWork(j, en2ctr, w);
      nonce = (uint32_t)((uint64_t)span * id);
      end = (uint64_t)nonce + span;
      hwCur = 0; hwDone = 0;
      have = true;
    }
    if (!have) { vTaskDelay(10); continue; }
    if (hwLoop >= 0 && id == hwId) {  // hardware SHA engine: full nonce range of its own extranonce2
      if (hwDone >= 0x100000000ULL) {
        en2ctr += nWorkers;
        buildWork(j, en2ctr, w);
        hwCur = 0; hwDone = 0;
      }
      double hpd = poolDiff; if (hpd <= 0) hpd = 1;
      uint32_t d = hwScan(w, (uint64_t)(DIFF1_TOP64 / hpd), hwCur);
      wst[id].hashes += d; hwDone += d;
      vTaskDelay(1 + pdMS_TO_TICKS(hwGapMs));
      continue;
    }
    if (nonce >= end) {  // range exhausted: next extranonce2
      en2ctr += nWorkers;
      buildWork(j, en2ctr, w);
      nonce = (uint32_t)((uint64_t)span * id);
      end = (uint64_t)nonce + span;
    }
    double pd = poolDiff; if (pd <= 0) pd = 1;
    uint64_t t64 = (uint64_t)(DIFF1_TOP64 / pd);
    uint32_t thr = (uint32_t)(t64 >> 32);
    if (thr < 0x000FFFFF) thr = 0x000FFFFF;  // also surface near-misses for "best diff"
    uint32_t cnt = (end - nonce) < BATCH ? (uint32_t)(end - nonce) : BATCH;
    while (cnt > 0) {
      uint32_t hit;
      if (scan_nonces(w, nonce, cnt, thr, &hit)) {
        uint32_t used = hit - nonce + 1;
        handleCandidate(w, hit, t64);
        wst[id].hashes += used; nonce += used; cnt -= used;
      } else {
        wst[id].hashes += cnt; nonce += cnt; cnt = 0;
      }
    }
    if (swGapMs) vTaskDelay(pdMS_TO_TICKS(swGapMs));
    else if (xPortGetCoreID() == 0) vTaskDelay(1);  // let WiFi/lwIP/web tasks breathe
  }
}

// ------------------------------------------------------------ stratum
static void sendLine(WiFiClient &c, const String &s) { c.print(s); c.print("\n"); }

static void onNotify(JsonArray p) {
  Job j;
  j.id = p[0].as<String>();
  std::vector<uint8_t> ph;
  hex2bytes(p[1] | "", ph);
  if (ph.size() != 32) return;
  for (int g = 0; g < 8; g++)
    for (int k = 0; k < 4; k++) j.prev[4 * g + k] = ph[4 * g + 3 - k];
  hex2bytes(p[2] | "", j.cb1);
  hex2bytes(p[3] | "", j.cb2);
  for (JsonVariant b : p[4].as<JsonArray>()) {
    std::vector<uint8_t> t; hex2bytes(b | "", t);
    if (t.size() == 32) j.branch.insert(j.branch.end(), t.begin(), t.end());
  }
  j.version = strtoul(p[5] | "0", nullptr, 16);
  j.nbits = strtoul(p[6] | "0", nullptr, 16);
  j.ntime = strtoul(p[7] | "0", nullptr, 16);
  xSemaphoreTake(jobMux, portMAX_DELAY);
  j.en1 = curJob.en1; j.en2Size = curJob.en2Size;
  curJob = j; jobSeq++; jobCount++;
  xSemaphoreGive(jobMux);
  poolReady = true;
  stage = "mining";
  LOGF("new job %s\n", j.id.c_str());
}

static void handleLine(const String &line, WiFiClient &cli) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  const char *method = doc["method"];
  if (method) {
    if (!strcmp(method, "mining.notify")) onNotify(doc["params"].as<JsonArray>());
    else if (!strcmp(method, "mining.set_difficulty")) {
      double d = doc["params"][0] | 1.0;
      if (d > 0) poolDiff = d;
    }
    return;
  }
  int id = doc["id"] | 0;
  if (id >= 1 && id <= 3) {
    size_t n = strlen(lastAuth);
    snprintf(lastAuth + n, sizeof(lastAuth) - n, "%s#%d: %s", n ? " || " : "", id, line.c_str());
  }
  if (id == 1) {
    JsonArray r = doc["result"].as<JsonArray>();
    if (r.isNull()) { strlcpy(lastErr, "subscribe failed", sizeof(lastErr)); return; }
    xSemaphoreTake(jobMux, portMAX_DELAY);
    hex2bytes(r[1] | "", curJob.en1);
    curJob.en2Size = r[2] | 4;
    xSemaphoreGive(jobMux);
    stage = "authorizing";
    LOGF("subscribed, authorizing\n");
    sendLine(cli, "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"" + cfg.addr + "." + cfg.worker + "\",\"" + cfg.pass + "\"]}");
    if (cfg.sugg.length()) sendLine(cli, "{\"id\":3,\"method\":\"mining.suggest_difficulty\",\"params\":[" + cfg.sugg + "]}");
  } else if (id == 2) {
    if (doc["result"] | false) { authDenied = false; authStreak = 0; stage = "waiting for first job"; LOGF("authorized\n"); }
    else {
      authDenied = true;
      JsonVariantConst er = doc["error"];
      String m = "login refused by pool";
      if (er.is<const char *>()) m += String(": ") + er.as<const char *>();
      else if (er.is<JsonArrayConst>() && er[1].is<const char *>()) m += String(": ") + er[1].as<const char *>();
      else if (er["message"].is<const char *>()) m += String(": ") + er["message"].as<const char *>();
      strlcpy(lastErr, m.c_str(), sizeof(lastErr));
      LOGF("authorize rejected\n");
    }
  } else if (id >= 100) {
    strlcpy(lastRep, line.c_str(), sizeof(lastRep));
    if (doc["result"] | false) { accepted++; lastErr[0] = 0; LOGF("share accepted (%u ok / %u bad)\n", (unsigned)accepted, (unsigned)rejected); }
    else {
      rejected++;
      JsonVariantConst er = doc["error"];
      String m;
      if (er.is<const char *>()) m = er.as<const char *>();
      else if (er.is<JsonArrayConst>() && er[1].is<const char *>()) m = er[1].as<const char *>();
      else if (er["message"].is<const char *>()) m = er["message"].as<const char *>();
      else m = "rejected, no reason given";
      strlcpy(lastErr, m.c_str(), sizeof(lastErr));
    }
  }
}

// Non-blocking line reader: collects bytes until '\n'. Returns true when a full line is in `line`.
static bool readStratumLine(WiFiClient &c, String &line) {
  while (c.available()) {
    int ch = c.read();
    if (ch < 0) break;
    if (ch == '\n') return true;
    if (ch != '\r' && line.length() < 6000) line += (char)ch;
  }
  return false;
}

static void stratumTask(void *) {
  static uint32_t submitId = 100;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED || !validAddr(cfg.addr)) { vTaskDelay(1000); continue; }
    parsePool(cfg.pool, poolHost, poolPort);
    WiFiClient cli;
    restartStratum = false;
    stage = "connecting to pool";
    if (!cli.connect(poolHost.c_str(), poolPort, 5000)) {
      strlcpy(lastErr, "cannot connect to pool", sizeof(lastErr));
      vTaskDelay(5000); continue;
    }
    cli.setNoDelay(true);
    poolConnected = true; poolReady = false; lastErr[0] = 0; authDenied = false; lastAuth[0] = 0;
    stage = "subscribing";
    LOGF("connected to %s:%u\n", poolHost.c_str(), (unsigned)poolPort);
    xQueueReset(shareQ);
    sendLine(cli, "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"CRSminer/1.0\"]}");
    uint32_t lastRx = millis();
    String rx;
    rx.reserve(1024);
    while (cli.connected() && !restartStratum) {
      Share s;
      while (xQueueReceive(shareQ, &s, 0) == pdTRUE) {
        if (authDenied) continue;  // pool refused our login: shares would only be rejected
        char buf[300];
        snprintf(buf, sizeof(buf),
                 "{\"id\":%u,\"method\":\"mining.submit\",\"params\":[\"%s.%s\",\"%s\",\"%s\",\"%08x\",\"%08x\"]}",
                 (unsigned)submitId++, cfg.addr.c_str(), cfg.worker.c_str(), s.job, s.en2, (unsigned)s.ntime, (unsigned)s.nonce);
        sendLine(cli, String(buf));
        strlcpy(lastSub, buf, sizeof(lastSub));
      }
      if (readStratumLine(cli, rx)) {
        if (rx.length()) handleLine(rx, cli);
        rx = "";
        lastRx = millis();
      } else vTaskDelay(5);
      if (authDenied) {  // a refused login may be a pool hiccup: drop the connection and try again later
        if (!authSince) authSince = millis() | 1;
        if (millis() - authSince > 60000) { if (authStreak < 250) authStreak++; break; }
      } else authSince = 0;
      if (millis() - lastRx > 300000) break;  // pool silent for 5 min: reconnect
    }
    cli.stop();
    if (authSince) { authSince = 0; vTaskDelay(27000); }
    poolConnected = false; poolReady = false;
    if (!lastErr[0]) strlcpy(lastErr, "pool closed the connection, reconnecting", sizeof(lastErr));
    stage = "reconnecting";
    LOGF("pool connection closed\n");
    vTaskDelay(3000);
  }
}

// ------------------------------------------------------------ web
static WebServer server(80);

static void handleStatus() {
  String s;
  s.reserve(1400);  // one allocation instead of many: avoids heap fragmentation over days
  s = "{";
  s += "\"hr\":" + String(totalRate, 0);
  s += ",\"cores\":[";
  for (int i = 0; i < nWorkers; i++) { if (i) s += ","; s += String(wst[i].rate, 0); }
  s += "],\"acc\":" + String(accepted) + ",\"rej\":" + String(rejected);
  s += ",\"best\":" + String(bestDiff, 3) + ",\"pdiff\":" + String(poolDiff, 9);
  s += ",\"up\":" + String(millis() / 1000);
  s += ",\"ok\":" + String(poolReady ? "true" : "false");
  s += ",\"pool\":\"" + poolHost + "\"";
  s += ",\"rssi\":" + String(WiFi.RSSI());
  s += ",\"heap\":" + String(ESP.getFreeHeap());
  s += ",\"temp\":" + String(temperatureRead(), 1);
  s += ",\"jobs\":" + String(jobCount);
  s += ",\"host\":\"" + hostName + ".local\"";
  s += ",\"mac\":\"" + WiFi.macAddress() + "\"";
  s += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  String e = selfOk ? String(lastErr) : String("SHA self-test failed - mining disabled");
  e.replace("\"", "'");
  s += ",\"stage\":\"" + String(selfOk ? stage : "disabled") + "\"";
  s += ",\"mode\":\"" + String(hwMode) + "\"";
  s += ",\"diag\":\"" + String(diagMsg) + "\"";
  { String a = lastSub, b = lastRep; a.replace("\\", "/"); b.replace("\\", "/"); a.replace("\"", "'"); b.replace("\"", "'");
    String c = lastAuth; c.replace("\\", "/"); c.replace("\"", "'");
    s += ",\"sub\":\"" + a + "\",\"rep\":\"" + b + "\",\"auth\":\"" + c + "\""; }
  s += ",\"gen\":" + String(gentle) + ",\"rst\":\"" + String(bootNote) + "\"";
  s += ",\"err\":\"" + e + "\"}";
  server.sendHeader("Access-Control-Allow-Origin", "*");  // read-only status: lets the Fleet monitor page read it
  server.send(200, "application/json", s);
}
static void handleGetCfg() {
  JsonDocument d;
  d["name"] = cfg.name; d["pool"] = cfg.pool; d["addr"] = cfg.addr; d["worker"] = cfg.worker; d["ledmode"] = cfg.ledMode; d["ledpin"] = cfg.ledPin; d["lang"] = cfg.lang; d["sugg"] = cfg.sugg;
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}
static void handlePostCfg() {
  String pool = server.arg("pool"), addr = server.arg("addr"), worker = server.arg("worker"), pass = server.arg("pass");
  String name = server.arg("name");
  pool.trim(); addr.trim(); worker.trim(); name.trim(); name.toLowerCase();
  if (!validName(name)) { server.send(400, "text/plain", "Miner name must be exactly 4 letters/digits"); return; }
  if (!validAddr(addr)) { server.send(400, "text/plain", "Invalid wallet address"); return; }
  if (pool.length() < 3) { server.send(400, "text/plain", "Invalid pool URL"); return; }
  for (size_t i = 0; i < worker.length(); i++)
    if (!isalnum((unsigned char)worker[i]) && worker[i] != '_' && worker[i] != '-') { server.send(400, "text/plain", "Worker: letters/digits/_/- only"); return; }
  String sugg = server.arg("sugg"); sugg.trim();
  if (!validSugg(sugg)) { server.send(400, "text/plain", "Suggested difficulty must be a number like 0.0001 (or blank)"); return; }
  int pinTmp = cfg.ledPin;
  { String lp = server.arg("ledpin"); lp.trim();
    if (lp.length() && (!parsePin(lp.c_str(), pinTmp) || !validLedPin(pinTmp))) { server.send(400, "text/plain", "LED pin not allowed on this board"); return; } }
  if (worker.length() == 0) worker = "crs";
  cfg.name = name; cfg.pool = pool; cfg.addr = addr; cfg.worker = worker;
  { int lm = server.arg("ledmode").toInt(); cfg.ledMode = (lm >= 0 && lm <= 2) ? lm : 0; }
  { String lp = server.arg("ledpin"); lp.trim();
    if (lp.length()) cfg.ledPin = pinTmp;  // validated above
  }
  cfg.lang = (server.arg("lang") == "1") ? 1 : 0;
  cfg.sugg = sugg;
  if (pass.length()) cfg.pass = pass;
  saveCfg();
  server.send(200, "text/plain", "Saved. Rebooting... new address: http://crsminer-" + name + ".local");
  delay(600);
  ESP.restart();
}
static void handleResetWifi() {
  server.send(200, "text/plain", "ok");
  delay(400);
  WiFiManager wm; wm.resetSettings();
  ESP.restart();
}
static void handleLog() {
  static EvEntry tmp[EV_MAX];
  uint8_t n = 0;
  xSemaphoreTake(evMux, portMAX_DELAY);
  n = evCount; memcpy(tmp, evBuf, sizeof(EvEntry) * n);
  xSemaphoreGive(evMux);
  String s;
  s.reserve(1000);
  s = "{\"boot\":" + String(bootNo) + ",\"ev\":[";
  for (int i = n - 1; i >= 0; i--) {  // newest first
    tmp[i].txt[sizeof(tmp[i].txt) - 1] = 0;
    String t = tmp[i].txt; t.replace("\"", "'"); t.replace("\\", "/");
    if (i != n - 1) s += ",";
    s += "{\"b\":" + String(tmp[i].boot) + ",\"u\":" + String(tmp[i].up) + ",\"t\":\"" + t + "\"}";
  }
  s += "]}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", s);
}
// Flood protection: a scanner or a badly written monitor can no longer use up the miner's CPU, sockets and memory.
static bool webBusy() {
  static uint32_t win = 0, cnt = 0;
  uint32_t now = millis();
  if (now - win >= 1000) { win = now; cnt = 0; }
  if (++cnt > 12) { server.send(429, "text/plain", "busy, slow down"); return true; }
  return false;
}
static void webTask(void *) {
  server.on("/", HTTP_GET, []() { if (!webBusy()) server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/license", HTTP_GET, []() { if (!webBusy()) server.send_P(200, "text/html", LICENSE_HTML); });
  server.on("/api/status", HTTP_GET, []() { if (!webBusy()) handleStatus(); });
  server.on("/api/log", HTTP_GET, []() { if (!webBusy()) handleLog(); });
  server.on("/api/config", HTTP_GET, []() { if (!webBusy()) handleGetCfg(); });
  server.on("/api/config", HTTP_POST, []() { if (!webBusy()) handlePostCfg(); });
  server.on("/api/resetwifi", HTTP_POST, []() { if (!webBusy()) handleResetWifi(); });
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  for (;;) { server.handleClient(); vTaskDelay(5); }
}

// ------------------------------------------------------------ LED / diagnostics
// Something that needs the user's attention? Every condition must persist for a while before it counts.
static const char *diagReason() {
  static uint32_t tSelf = 0, tWifi = 0, tPool = 0, tAuth = 0, tHash = 0, tRej = 0, readySince = 0;
  uint32_t now = millis();
  auto held = [&](bool cond, uint32_t &since, uint32_t ms) {
    if (!cond) { since = 0; return false; }
    if (!since) since = now ? now : 1;
    return now - since >= ms;
  };
  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (poolReady) { if (!readySince) readySince = now ? now : 1; } else readySince = 0;
  if (held(!selfOk, tSelf, 10000)) return "SHA self-test failed";
  if (held(!wifiUp, tWifi, 10000)) return "WiFi / internet not connected";
  if (held(authDenied || authStreak >= 2, tAuth, 3000)) return "pool refused the login (check wallet address)";
  if (held(wifiUp && !poolConnected, tPool, 15000)) return "pool unreachable";
  if (held(poolReady && totalRate < 1000, tHash, 30000)) return "no hashing";
  static uint32_t snapT = 0, sA[3] = {0, 0, 0}, sR[3] = {0, 0, 0};  // rejects are judged over the last ~20-30 min
  if (!snapT || now - snapT >= 600000UL) {
    sA[0] = sA[1]; sA[1] = sA[2]; sA[2] = accepted;
    sR[0] = sR[1]; sR[1] = sR[2]; sR[2] = rejected;
    snapT = now ? now : 1;
  }
  { uint32_t dA = accepted - sA[0], dR = rejected - sR[0];
    if (held(dR >= 10 && dR > dA, tRej, 1000)) return "most shares are rejected"; }
  if (readySince && accepted == 0 && totalRate > 0) {  // no accepted share for far longer than expected
    double etaMs = (double)poolDiff * 4294967296.0 / (double)totalRate * 1000.0;
    double limit = etaMs * 8.0;
    if (limit < 900000.0) limit = 900000.0;
    if ((double)(now - readySince) > limit) return "no accepted shares for a long time";
  }
  return "";
}

// LED modes: 0 = leave the board alone, 1 = dark + quiet serial,
// 2 = diagnostic: blink for 20 s, then dark; steady on whenever something needs attention.
static void ledTask(void *) {
  bool level = false;
  for (;;) {
    const char *why = diagReason();
    diagMsg = why;
    {  // log problems / recoveries (at most one flash write per 30 s)
      static char seen[48] = "";
      static uint32_t lastT = 0;
      if (strcmp(why, seen) && millis() - lastT > 30000) {
        lastT = millis();
        char b[48];
        if (why[0]) { snprintf(b, sizeof(b), "! %s", why); evLog(b); }
        else if (seen[0]) evLog("ok: recovered");
        strlcpy(seen, why, sizeof(seen));
      }
    }
    uint8_t m = cfg.ledMode;
    int want = -1;  // -1 = leave pin alone, 0 = low, 1 = high
    if (m == 1) { ledQuiet = true; want = 0; }
    else if (m == 2) {
      if (why[0]) { ledQuiet = false; want = 1; }
      else if (millis() < 20000) { ledQuiet = false; level = !level; want = level ? 1 : 0; }
      else { ledQuiet = true; want = 0; }
    } else ledQuiet = false;
    int pin = cfg.ledPin;
    if (pin >= 0 && want >= 0) { pinMode(pin, OUTPUT); digitalWrite(pin, (want != 0) != (CRS_LED_ACTIVE_LOW != 0) ? HIGH : LOW); }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

// ------------------------------------------------------------ stats / selftest
static void statsTask(void *) {
  for (;;) {
    vTaskDelay(1000);
    applyLed();
    float tot = 0;
    for (int i = 0; i < nWorkers; i++) {
      uint32_t h = wst[i].hashes;
      wst[i].rate = (float)(uint32_t)(h - wst[i].last);
      wst[i].last = h;
      tot += wst[i].rate;
    }
    totalRate = tot;

    // ---- watchdogs: recover by themselves from states that would otherwise last forever
    static bool eased = false;
    if (!eased && gentle && millis() > 86400000UL) {  // 24 h without a brownout: relax one level at the next restart
      eased = true;
      Preferences bp;
      if (bp.begin("crsrst", false)) { uint8_t bo = bp.getUChar("bo", 0); if (bo > 0) bp.putUChar("bo", bo - 1); bp.end(); }
      evLog("ok: stable 24 h, power mode eased");
    }
    static uint32_t wifiDown = 0, poolDown = 0, noHash = 0, hwStall = 0, lowHeap = 0;
    bool up = WiFi.status() == WL_CONNECTED;
    if (up) wifiDown = 0;
    else {
      wifiDown++;
      if (wifiDown == 20) WiFi.reconnect();
      if (wifiDown == 90) { WiFi.disconnect(false); WiFi.begin(); }
      if (wifiDown >= 600) crsRestart("WiFi lost for 10 min");
    }
    if (up && selfOk && !poolConnected && validAddr(cfg.addr)) { if (++poolDown >= 900) crsRestart("pool unreachable for 15 min"); }
    else poolDown = 0;
    if (poolReady && selfOk && totalRate < 1000) { if (++noHash >= 120) crsRestart("hashing stalled"); }
    else noHash = 0;
    if (poolReady && hwLoop >= 0 && hwId >= 0 && wst[hwId].rate < 1) { if (++hwStall >= 60) crsRestart("hardware SHA stalled"); }
    else hwStall = 0;
    if (ESP.getFreeHeap() < 16000 || ESP.getMaxAllocHeap() < 5000) { if (++lowHeap >= 60) crsRestart("low memory"); }
    else lowHeap = 0;
  }
}

static bool selfTest() {
  uint8_t out[32];
  sha256_bytes((const uint8_t *)"abc", 3, out);  // SHA-256("abc") = ba7816bf...f20015ad
  if (out[0] != 0xba || out[1] != 0x78 || out[31] != 0xad) return false;
  // Bitcoin genesis block: nonce 2083236893 must hash to 00000000 19d6689c...
  std::vector<uint8_t> h;
  hex2bytes("0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d1dac2b7c", h);
  Work w; midstateFromHeader(h.data(), w);
  const uint32_t N = 2083236893UL;
  uint32_t top, h6;
  hash_full(w, N, &top, &h6);
  if (top != 0 || h6 != 0x0019d668) return false;  // genesis hash = 00000000 0019d668 9c085ae1 ...
  uint32_t hit = 0;
  if (!scan_nonces(w, N - 5, 10, 0, &hit) || hit != N) return false;
  return true;
}

// ------------------------------------------------------------ CYD display task
static String uiApName;
static void uiTask(void *) {
  static UiSnap s;
  for (;;) {
    memset(&s, 0, sizeof(s));
    strlcpy(s.title, "CRSminer", sizeof(s.title));
    snprintf(s.host, sizeof(s.host), "%s.local", hostName.c_str());
    s.hr = totalRate;
    s.nCores = nWorkers;
    for (int i = 0; i < nWorkers && i < 4; i++) s.cores[i] = wst[i].rate;
    uiLedEnabled = (cfg.ledMode != 1);
    s.acc = accepted; s.rej = rejected; s.up = millis() / 1000;
    s.ok = poolReady;
    s.rssi = WiFi.RSSI(); s.temp = temperatureRead(); s.heap = ESP.getFreeHeap();
    strlcpy(s.ip, WiFi.localIP().toString().c_str(), sizeof(s.ip));
    strlcpy(s.mac, WiFi.macAddress().c_str(), sizeof(s.mac));
    if (poolReady) snprintf(s.pool, sizeof(s.pool), "%s:%u", poolHost.c_str(), (unsigned)poolPort);
    else strlcpy(s.pool, selfOk ? stage : "disabled", sizeof(s.pool));
    strlcpy(s.err, selfOk ? lastErr : "SHA self-test failed", sizeof(s.err));
    strlcpy(s.extraL, "Best diff", sizeof(s.extraL));
    ui_si(bestDiff, s.extraV, sizeof(s.extraV));
    ui_update(s);
    vTaskDelay(pdMS_TO_TICKS(60));  // fast enough to catch taps; redraws are cached
  }
}

// ------------------------------------------------------------ setup / loop
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nCRSminer starting");
  ui_begin();
  ui_splash("CRSminer", "starting...", "tap the screen to switch pages");
  pinMode(CRS_BOOT_PIN, INPUT_PULLUP);
  evMux = xSemaphoreCreateMutex();
  evLoad();
  {
    Preferences rp;
    String why; unsigned n = 0;
    if (rp.begin("crsrst", false)) { why = rp.getString("why", ""); n = rp.getUInt("n", 0); rp.remove("why"); rp.end(); }
    esp_reset_reason_t rr = esp_reset_reason();
    const char *rs = rr == ESP_RST_POWERON ? "power on" : rr == ESP_RST_BROWNOUT ? "BROWNOUT (power supply too weak)"
                   : rr == ESP_RST_PANIC ? "crash (panic)" : (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) ? "watchdog reset"
                   : rr == ESP_RST_EXT ? "reset button" : "software restart";
    if (why.length()) snprintf(bootNote, sizeof(bootNote), "%s (self-heal #%u)", why.c_str(), n);
    else strlcpy(bootNote, rs, sizeof(bootNote));
    char b[48]; snprintf(b, sizeof(b), "start: %s", bootNote); evLog(b);
    {  // brownout streak -> gentle power mode level (kept in flash, eased after 24 h of stable running)
      Preferences bp;
      if (bp.begin("crsrst", false)) {
        uint8_t bo = bp.getUChar("bo", 0);
        if (rr == ESP_RST_BROWNOUT && bo < 3) { bo++; bp.putUChar("bo", bo); }
        gentle = bo;
        bp.end();
      }
      static const uint16_t hwG[4] = {0, 15, 40, 100}, swG[4] = {0, 60, 150, 400};
      hwGapMs = hwG[gentle]; swGapMs = swG[gentle];
      if (gentle) { snprintf(b, sizeof(b), "power: gentle mode %u", (unsigned)gentle); evLog(b); }
    }
  }
  loadCfg();
  applyLed();
  xTaskCreatePinnedToCore(ledTask, "led", 4096, nullptr, 1, nullptr, 0);

  // Hold BOOT for 3 s at power-up to factory reset
  uint32_t t0 = millis();
  while (digitalRead(CRS_BOOT_PIN) == LOW) {
    if (millis() - t0 > 3000) {
      Serial.println("Factory reset");
      WiFiManager wm; wm.resetSettings();
      prefs.begin("crsminer", false); prefs.clear(); prefs.end();
      ESP.restart();
    }
    delay(10);
  }

  selfOk = selfTest();
  Serial.println(selfOk ? "SHA self-test OK" : "SHA self-test FAILED");
  ui_splash("CRSminer", selfOk ? "self-test OK" : "self-test FAILED", "connecting to WiFi...");
  if (selfOk) hwSelect();
  Serial.printf("SHA engine: %s\n", hwMode);

  WiFi.mode(WIFI_STA);
  applyTxPower();
  WiFi.setAutoReconnect(true);
  WiFiManager wm;
  wm.setTitle("CRSminer Setup");
  wm.setSaveConfigCallback([]() { shouldSave = true; });
  wm.setSaveParamsCallback([]() { shouldSave = true; });
  const bool gr = cfg.lang == 1;
  auto L = [&](const char *en, const char *el) { return gr ? el : en; };
  WiFiManagerParameter pName("name", L("Miner name (exactly 4 letters/digits, required)", "Όνομα miner (ακριβώς 4 γράμματα/ψηφία, υποχρεωτικό)"), cfg.name.c_str(), 4,
                             "required minlength=4 maxlength=4 pattern=\"[A-Za-z0-9]{4}\" placeholder=\"e.g. rig1\"");
  WiFiManagerParameter pPool("pool", L("Pool URL (host:port)", "Διεύθυνση pool (host:port)"), cfg.pool.c_str(), 100);
  WiFiManagerParameter pAddr("addr", L("Wallet address for the coin you mine (BTC, DGB, ...)", "Διεύθυνση πορτοφολιού για το νόμισμα που εξορύσσετε (BTC, DGB, ...)"), cfg.addr.c_str(), 90);
  WiFiManagerParameter pWorker("worker", L("Worker name", "Όνομα worker"), cfg.worker.c_str(), 30);
  WiFiManagerParameter pPass("pass", L("Pool password (usually x)", "Κωδικός pool (συνήθως x)"), cfg.pass.c_str(), 30);
  WiFiManagerParameter pSugg("sugg", L("Suggested pool difficulty (optional, e.g. 0.0001; blank = pool default)", "Προτεινόμενη δυσκολία pool (προαιρετικό, π.χ. 0.0001· κενό = προεπιλογή pool)"), cfg.sugg.c_str(), 12);
  // 3-way LED mode and language: a <select> that fills a hidden field (values are read back from the hidden field)
  static char ledHtml[900], langHtml[520], ledVal[2], langVal[2];
  snprintf(ledVal, sizeof(ledVal), "%u", (unsigned)cfg.ledMode);
  snprintf(langVal, sizeof(langVal), "%u", (unsigned)cfg.lang);
  snprintf(ledHtml, sizeof(ledHtml),
           "<br/><label>%s</label><select onchange=\"document.getElementById('ledmode').value=this.value\">"
           "<option value=\"0\"%s>%s</option><option value=\"1\"%s>%s</option><option value=\"2\"%s>%s</option></select>",
           L("Blue LED", "Μπλε λυχνία LED"),
           cfg.ledMode == 0 ? " selected" : "", L("On (default)", "Αναμμένη (προεπιλογή)"),
           cfg.ledMode == 1 ? " selected" : "", L("Off (also silences serial output)", "Σβηστή (σιωπά και η σειριακή έξοδος)"),
           cfg.ledMode == 2 ? " selected" : "", L("Diagnostic (blinks 20 s, then off; steady on = needs attention)", "Διαγνωστική (αναβοσβήνει 20 δλ, μετά σβήνει· σταθερά αναμμένη = χρειάζεται ενέργεια)"));
  snprintf(langHtml, sizeof(langHtml),
           "<br/><label>Language / Γλώσσα</label><select onchange=\"document.getElementById('lang').value=this.value\">"
           "<option value=\"0\"%s>English</option><option value=\"1\"%s>Ελληνικά</option></select>",
           cfg.lang == 0 ? " selected" : "", cfg.lang == 1 ? " selected" : "");
  static char ledPinVal[5];
  snprintf(ledPinVal, sizeof(ledPinVal), "%d", cfg.ledPin);
  WiFiManagerParameter pLedPin("ledpin", L("LED GPIO pin (2 = most boards; -1 = no LED pin)", "Pin GPIO της LED (2 = οι περισσότερες πλακέτες· -1 = καμία)"), ledPinVal, 3,
                               "type=\"number\" min=\"-1\" max=\"48\" step=\"1\"");
  WiFiManagerParameter pLedSel(ledHtml);
  WiFiManagerParameter pLedVal("ledmode", "", ledVal, 1, "type=\"hidden\"");
  WiFiManagerParameter pLangSel(langHtml);
  WiFiManagerParameter pLangVal("lang", "", langVal, 1, "type=\"hidden\"");
  wm.addParameter(&pName); wm.addParameter(&pPool); wm.addParameter(&pAddr); wm.addParameter(&pWorker); wm.addParameter(&pPass); wm.addParameter(&pSugg);
  wm.addParameter(&pLedSel); wm.addParameter(&pLedVal); wm.addParameter(&pLedPin); wm.addParameter(&pLangSel); wm.addParameter(&pLangVal);

  auto pull = [&]() {
    cfg.name = pName.getValue(); cfg.name.trim(); cfg.name.toLowerCase();
    cfg.pool = pPool.getValue(); cfg.addr = pAddr.getValue();
    cfg.worker = pWorker.getValue(); cfg.pass = pPass.getValue();
    { int lm = atoi(pLedVal.getValue()); cfg.ledMode = (lm >= 0 && lm <= 2) ? lm : 0; }
    cfg.lang = (atoi(pLangVal.getValue()) == 1) ? 1 : 0;
    { int lp; if (parsePin(pLedPin.getValue(), lp) && validLedPin(lp)) cfg.ledPin = lp; }
    cfg.sugg = pSugg.getValue(); cfg.sugg.trim();
    if (!validSugg(cfg.sugg)) cfg.sugg = "";
    cfg.pool.trim(); cfg.addr.trim(); cfg.worker.trim();
    if (!cfg.worker.length()) cfg.worker = "crs";
    saveCfg();
  };
  String apName = validName(cfg.name) ? "CRSminer-" + cfg.name + "-Setup" : String("CRSminer-Setup");
  uiApName = apName;
  wm.setAPCallback([](WiFiManager *) { ui_portal(uiApName.c_str(), WiFi.softAPIP().toString().c_str()); });
  bool ok;
  if (!validAddr(cfg.addr) || !validName(cfg.name)) ok = wm.startConfigPortal(apName.c_str());  // missing info: always ask
  else ok = wm.autoConnect(apName.c_str());
  applyTxPower();
  if (shouldSave) pull();
  while (!validAddr(cfg.addr) || !validName(cfg.name) || !ok) {  // keep asking until valid
    ok = wm.startConfigPortal(apName.c_str());
    if (shouldSave) pull();
  }
  hostName = "crsminer-" + cfg.name;
  applyLed();
  Serial.print("IP: "); Serial.println(WiFi.localIP());
  Serial.print("MAC: "); Serial.println(WiFi.macAddress());
  if (MDNS.begin(hostName.c_str())) Serial.println("http://" + hostName + ".local");

  jobMux = xSemaphoreCreateMutex();
  shareQ = xQueueCreate(16, sizeof(Share));
  nWorkers = ESP.getChipCores();
  if (nWorkers < 1) nWorkers = 1;
  if (nWorkers > MAX_WORKERS) nWorkers = MAX_WORKERS;
  hwId = (hwLoop >= 0) ? nWorkers - 1 : -1;  // hardware SHA runs on the last core (core 1)
  parsePool(cfg.pool, poolHost, poolPort);

  xTaskCreatePinnedToCore(statsTask, "stats", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(webTask, "web", 6144, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(uiTask, "ui", 6144, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(stratumTask, "stratum", 10240, nullptr, 3, nullptr, 0);
  for (int i = 0; i < nWorkers; i++)
    xTaskCreatePinnedToCore(workerTask, "miner", 8192, (void *)(intptr_t)i, 1, nullptr, i);
}

void loop() {
  vTaskDelay(5000);
  LOGF("%.1f kH/s | acc %u rej %u | best %.1f | diff %.0f\n",
                totalRate / 1000.0, (unsigned)accepted, (unsigned)rejected, bestDiff, poolDiff);
}

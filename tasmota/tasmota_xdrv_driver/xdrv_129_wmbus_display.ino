/*
  xdrv_129_wmbus_display.ino - OLED HUD for the Multical21 / wM-Bus radio stack
                               on Heltec WiFi LoRa 32 V3 (SSD1306 128x64 I²C).

  Architecture
  ------------
                +----------------------+        +-------------------------+
   SX1262 ----> | xdrv_128_wmbus_radio | -----> | xsns_121_multical21     |
                +----------------------+ frame  +-------------------------+
                                                          |
                                                          | M21GetDisplaySnapshot()
                                                          v
                                                +-------------------------+
   I²C OLED <-- | xdrv_129_wmbus_display | ---- | (this driver)           |
                +-------------------------+      +-------------------------+

  Behaviour
  ---------
  * FUNC_PRE_INIT: pulls the configured Vext-control pin LOW (powers OLED+LoRa
    on the Heltec V3), then probes 0x3C on the I²C bus. If found, the panel is
    initialised; otherwise the driver self-disables.
  * FUNC_EVERY_SECOND: ticks the page rotation counter; redraws on data change
    or every WMBUS_OLED_REFRESH_SEC seconds (default 5).
  * FUNC_COMMAND: exposes `OledPage <0..n>` to lock a single page, `OledOff` /
    `OledOn` for manual blanking, and `OledDim <0..255>` for contrast.

  Page layout (auto-rotated every 5 s)
  ------------------------------------
   Page 0 - Network:        IP, WiFi RSSI, uptime, frame counts
   Page 1 - Meter:          total m³, last update age, RSSI of last telegram
   Page 2 - Debug/details:  serial, target m³, flow/ambient temp

  Build flags
  -----------
    -DUSE_WMBUS_OLED                       (enables this driver)
    -DWMBUS_OLED_I2C_ADDR=0x3C             (default 0x3C, override if 0x3D)
    -DWMBUS_OLED_REFRESH_SEC=5             (full redraw cadence)
    -DWMBUS_OLED_VEXT_PIN=36               (Heltec V3 Vext-EN, -1 to disable)
    -DWMBUS_OLED_VEXT_ACTIVE_LOW=1         (Heltec V3: 0=on, 1=off)
    -DWMBUS_OLED_DIM_TIMEOUT_SEC=300       (0 disables dimming after no input)

  License: same as Tasmota (Apache-2.0 / GPL dual; this file: Apache-2.0).
*/

#ifdef USE_WMBUS_OLED
#ifdef USE_I2C

#define XDRV_129  129

#include <Wire.h>
#include "wmbus_oled.h"
#include "wmbus_display_snapshot.h"

#ifndef WMBUS_OLED_I2C_ADDR
#define WMBUS_OLED_I2C_ADDR        0x3C
#endif
#ifndef WMBUS_OLED_REFRESH_SEC
#define WMBUS_OLED_REFRESH_SEC     5
#endif
#ifndef WMBUS_OLED_VEXT_PIN
#define WMBUS_OLED_VEXT_PIN        36
#endif
#ifndef WMBUS_OLED_VEXT_ACTIVE_LOW
#define WMBUS_OLED_VEXT_ACTIVE_LOW 1
#endif
// SSD1306 hardware reset line. On the Heltec WiFi LoRa 32 V3 the OLED RST
// pin is wired to GPIO21 and MUST be pulsed after power-up, otherwise the
// controller stays in reset and the panel remains blank even though it ACKs
// on I²C. Set to -1 if your board ties RST to VCC and needs no toggling.
#ifndef WMBUS_OLED_RST_PIN
#define WMBUS_OLED_RST_PIN         21
#endif
#ifndef WMBUS_OLED_DIM_TIMEOUT_SEC
#define WMBUS_OLED_DIM_TIMEOUT_SEC 300
#endif

#define WMBUS_OLED_PAGE_COUNT      3

/*-------------------------------------------------------------------------------------------*\
 * Driver state
\*-------------------------------------------------------------------------------------------*/

struct WMBusOledState {
  WmbusOled oled;
  bool      ready;
  uint8_t   page;
  uint8_t   page_lock;          // 0xFF = rotate, otherwise locked page
  uint8_t   sec_since_redraw;
  uint32_t  last_frames_valid;
  uint8_t   contrast;
  bool      power_on;
  uint16_t  sec_since_input;
  bool      dimmed;
};

static WMBusOledState *WOled = nullptr;

#define WOLED_LOG(level, fmt, ...) AddLog((level), PSTR("WOLED: " fmt), ##__VA_ARGS__)

/*-------------------------------------------------------------------------------------------*\
 * Hardware setup
\*-------------------------------------------------------------------------------------------*/

static void WMBusOledVextPower(bool on) {
#if WMBUS_OLED_VEXT_PIN >= 0
  const int active_state = WMBUS_OLED_VEXT_ACTIVE_LOW ? LOW : HIGH;
  pinMode(WMBUS_OLED_VEXT_PIN, OUTPUT);
  digitalWrite(WMBUS_OLED_VEXT_PIN, on ? active_state : !active_state);
  delay(50);                                  // give the panel its boot-time
#else
  (void)on;
#endif
}

// Pulse the SSD1306 hardware reset line. Heltec V3 wires this to GPIO21 and
// the panel will not leave reset until this line is driven low->high.
static void WMBusOledHwReset(void) {
#if WMBUS_OLED_RST_PIN >= 0
  pinMode(WMBUS_OLED_RST_PIN, OUTPUT);
  digitalWrite(WMBUS_OLED_RST_PIN, HIGH);
  delay(1);
  digitalWrite(WMBUS_OLED_RST_PIN, LOW);
  delay(10);
  digitalWrite(WMBUS_OLED_RST_PIN, HIGH);
  delay(10);
#endif
}

static void WMBusOledInit(void) {
  if (WOled) { return; }
  WOled = (WMBusOledState *)calloc(1, sizeof(*WOled));
  if (!WOled) {
    WOLED_LOG(LOG_LEVEL_ERROR, "alloc failed");
    return;
  }
  WOled->page_lock        = 0xFF;
  WOled->contrast         = 0xCF;
  WOled->power_on         = true;
  WOled->sec_since_redraw = 0xFF;             // force first redraw

  WMBusOledVextPower(true);
  WMBusOledHwReset();

  // I²C bus is brought up by Tasmota in FUNC_PRE_INIT when GPIO_I2C_SDA/SCL
  // are configured; we just probe directly.

  if (!WOled->oled.begin(Wire, WMBUS_OLED_I2C_ADDR)) {
    WOLED_LOG(LOG_LEVEL_INFO, "no SSD1306 ACK at 0x%02X", WMBUS_OLED_I2C_ADDR);
    free(WOled);
    WOled = nullptr;
    return;
  }
  WOled->ready = true;
  WOLED_LOG(LOG_LEVEL_INFO, "SSD1306 ready at 0x%02X (Vext pin=%d RST pin=%d)",
            WMBUS_OLED_I2C_ADDR, (int)WMBUS_OLED_VEXT_PIN, (int)WMBUS_OLED_RST_PIN);
}

/*-------------------------------------------------------------------------------------------*\
 * Rendering helpers
\*-------------------------------------------------------------------------------------------*/

static void WMBusOledHeader(const char *title, int8_t wifi_rssi) {
  // Inverted top bar with title left, WiFi RSSI right.
  for (uint8_t col = 0; col < WmbusOled::kCols; ++col) {
    WOled->oled.drawChar(col, 0, ' ', /*invert=*/true);
  }
  WOled->oled.drawString(0, 0, title, /*invert=*/true);
  if (wifi_rssi != 0) {
    char buf[10];
    snprintf(buf, sizeof(buf), "%ddB", (int)wifi_rssi);
    const uint8_t len = (uint8_t)strlen(buf);
    if (len < WmbusOled::kCols) {
      WOled->oled.drawString(WmbusOled::kCols - len, 0, buf, /*invert=*/true);
    }
  }
}

// Signature uses const void* so the Arduino auto-prototype generator does not
// need the M21DisplaySnapshot struct definition to be visible at the top of
// the concatenated translation unit.
static void WMBusOledRenderNetwork(const void *snap_ptr) {
  const M21DisplaySnapshot &s = *(const M21DisplaySnapshot *)snap_ptr;
  WOled->oled.clear();
  const int8_t wifi_rssi = (WiFi.status() == WL_CONNECTED) ? (int8_t)WiFi.RSSI() : 0;
  WMBusOledHeader("Network", wifi_rssi);

  char line[24];
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("---");
  snprintf(line, sizeof(line), "IP %s", ip.c_str());
  WOled->oled.drawString(0, 2, line);

  const uint32_t up_s = millis() / 1000;
  snprintf(line, sizeof(line), "Up   %02u:%02u:%02u",
           (unsigned)(up_s / 3600),
           (unsigned)((up_s / 60) % 60),
           (unsigned)(up_s % 60));
  WOled->oled.drawString(0, 4, line);

  snprintf(line, sizeof(line), "Rx   %u / %u",
           (unsigned)s.frames_valid, (unsigned)s.frames_total);
  WOled->oled.drawString(0, 6, line);
}

static void WMBusOledRenderMeter(const void *snap_ptr) {
  const M21DisplaySnapshot &s = *(const M21DisplaySnapshot *)snap_ptr;
  WOled->oled.clear();
  const int8_t wifi_rssi = (WiFi.status() == WL_CONNECTED) ? (int8_t)WiFi.RSSI() : 0;
  WMBusOledHeader("Water", wifi_rssi);

  if (!s.configured) {
    WOled->oled.drawString(0, 3, "Not configured");
    WOled->oled.drawString(0, 5, "M21Key / M21Id");
    return;
  }
  if (!s.have_data) {
    WOled->oled.drawString(0, 3, "Waiting for");
    WOled->oled.drawString(0, 4, "Multical21 data...");
    char rssi[20];
    snprintf(rssi, sizeof(rssi), "RSSI %d dBm", (int)s.last_rssi_dbm);
    WOled->oled.drawString(0, 7, rssi);
    return;
  }

  char line[24];
  snprintf(line, sizeof(line), "%.3f m3", s.total_m3);
  WOled->oled.drawString(0, 3, line);                 // large-ish primary value

  const uint32_t age_s = (millis() - s.last_valid_ms) / 1000;
  if (age_s < 3600) {
    snprintf(line, sizeof(line), "Age  %us", (unsigned)age_s);
  } else {
    snprintf(line, sizeof(line), "Age  %um", (unsigned)(age_s / 60));
  }
  WOled->oled.drawString(0, 5, line);

  snprintf(line, sizeof(line), "RSSI %d dBm", (int)s.last_rssi_dbm);
  WOled->oled.drawString(0, 6, line);

  snprintf(line, sizeof(line), "Flow %dC  Amb %dC",
           (int)s.flow_temp_c, (int)s.ambient_temp_c);
  WOled->oled.drawString(0, 7, line);
}

static void WMBusOledRenderDebug(const void *snap_ptr) {
  const M21DisplaySnapshot &s = *(const M21DisplaySnapshot *)snap_ptr;
  WOled->oled.clear();
  WMBusOledHeader("Debug", 0);

  char line[24];
  snprintf(line, sizeof(line), "Target %.3f m3", s.target_m3);
  WOled->oled.drawString(0, 2, line);

  snprintf(line, sizeof(line), "Frames v=%u t=%u",
           (unsigned)s.frames_valid, (unsigned)s.frames_total);
  WOled->oled.drawString(0, 4, line);

  // Live WiFi info on its own row
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(line, sizeof(line), "WiFi %d dBm", (int)WiFi.RSSI());
    WOled->oled.drawString(0, 6, line);
    String ssid = WiFi.SSID();
    if (ssid.length() > WmbusOled::kCols) ssid.remove(WmbusOled::kCols);
    WOled->oled.drawString(0, 7, ssid.c_str());
  } else {
    WOled->oled.drawString(0, 6, "WiFi offline");
  }
}

static void WMBusOledRender() {
  if (!WOled || !WOled->ready) { return; }

  M21DisplaySnapshot snap = {};
  const bool ok = M21GetDisplaySnapshot(&snap);
  if (!ok) {
    WOled->oled.clear();
    WOled->oled.drawString(0, 3, "M21 driver idle");
    WOled->oled.display();
    return;
  }

  const uint8_t page = (WOled->page_lock != 0xFF) ? WOled->page_lock : WOled->page;
  switch (page) {
    case 0:  WMBusOledRenderNetwork(&snap); break;
    case 1:  WMBusOledRenderMeter(&snap);   break;
    default: WMBusOledRenderDebug(&snap);   break;
  }
  WOled->oled.display();
  WOled->sec_since_redraw = 0;
}

/*-------------------------------------------------------------------------------------------*\
 * Periodic tick (called from FUNC_EVERY_SECOND)
\*-------------------------------------------------------------------------------------------*/

static void WMBusOledTick() {
  if (!WOled || !WOled->ready) { return; }

  // Force redraw on new valid frame
  M21DisplaySnapshot snap = {};
  if (M21GetDisplaySnapshot(&snap)) {
    if (snap.frames_valid != WOled->last_frames_valid) {
      WOled->last_frames_valid = snap.frames_valid;
      WOled->sec_since_redraw  = 0xFF;             // trigger redraw below
      WOled->sec_since_input   = 0;                // count as user-relevant
    }
  }

  // Page rotation: advance every WMBUS_OLED_REFRESH_SEC seconds when unlocked.
  if (WOled->page_lock == 0xFF) {
    if (WOled->sec_since_redraw >= WMBUS_OLED_REFRESH_SEC) {
      WOled->page = (uint8_t)((WOled->page + 1) % WMBUS_OLED_PAGE_COUNT);
      WMBusOledRender();
    } else {
      ++WOled->sec_since_redraw;
    }
  } else {
    // Locked page: still refresh on cadence to keep age/RSSI fresh.
    if (WOled->sec_since_redraw >= WMBUS_OLED_REFRESH_SEC) {
      WMBusOledRender();
    } else {
      ++WOled->sec_since_redraw;
    }
  }

  // Optional dim after timeout (suppressed when 0)
#if WMBUS_OLED_DIM_TIMEOUT_SEC > 0
  if (WOled->power_on) {
    if (WOled->sec_since_input < 0xFFFE) ++WOled->sec_since_input;
    if (!WOled->dimmed && WOled->sec_since_input >= WMBUS_OLED_DIM_TIMEOUT_SEC) {
      WOled->oled.setContrast(0x10);
      WOled->dimmed = true;
    } else if (WOled->dimmed && WOled->sec_since_input < WMBUS_OLED_DIM_TIMEOUT_SEC) {
      WOled->oled.setContrast(WOled->contrast);
      WOled->dimmed = false;
    }
  }
#endif
}

/*-------------------------------------------------------------------------------------------*\
 * Commands
\*-------------------------------------------------------------------------------------------*/

#define D_CMND_OLED_PAGE  "OledPage"
#define D_CMND_OLED_ON    "OledOn"
#define D_CMND_OLED_OFF   "OledOff"
#define D_CMND_OLED_DIM   "OledDim"

// Forward declarations so the dispatch table below can take their addresses.
void CmndOledPage(void);
void CmndOledOn(void);
void CmndOledOff(void);
void CmndOledDim(void);

const char kWMBusOledCommands[] PROGMEM = "|"
  D_CMND_OLED_PAGE "|" D_CMND_OLED_ON "|" D_CMND_OLED_OFF "|" D_CMND_OLED_DIM;

void (* const WMBusOledCommand[])(void) PROGMEM = {
  &CmndOledPage, &CmndOledOn, &CmndOledOff, &CmndOledDim
};

void CmndOledPage(void) {
  if (!WOled) { ResponseCmndChar("not-ready"); return; }
  if (XdrvMailbox.data_len > 0) {
    int v = atoi(XdrvMailbox.data);
    if (v < 0)                          WOled->page_lock = 0xFF;
    else if (v >= WMBUS_OLED_PAGE_COUNT) WOled->page_lock = WMBUS_OLED_PAGE_COUNT - 1;
    else                                 WOled->page_lock = (uint8_t)v;
    WOled->sec_since_input = 0;
    WMBusOledRender();
  }
  ResponseCmndNumber((WOled->page_lock == 0xFF) ? -1 : (int)WOled->page_lock);
}

void CmndOledOn(void) {
  if (!WOled) { ResponseCmndChar("not-ready"); return; }
  WOled->oled.powerOn();
  WOled->oled.setContrast(WOled->contrast);
  WOled->power_on        = true;
  WOled->dimmed          = false;
  WOled->sec_since_input = 0;
  ResponseCmndDone();
}

void CmndOledOff(void) {
  if (!WOled) { ResponseCmndChar("not-ready"); return; }
  WOled->oled.powerOff();
  WOled->power_on = false;
  ResponseCmndDone();
}

void CmndOledDim(void) {
  if (!WOled) { ResponseCmndChar("not-ready"); return; }
  if (XdrvMailbox.data_len > 0) {
    int v = atoi(XdrvMailbox.data);
    if (v < 0) v = 0; else if (v > 255) v = 255;
    WOled->contrast = (uint8_t)v;
    WOled->oled.setContrast(WOled->contrast);
    WOled->dimmed = false;
  }
  ResponseCmndNumber((int)WOled->contrast);
}

/*-------------------------------------------------------------------------------------------*\
 * Tasmota driver dispatch
\*-------------------------------------------------------------------------------------------*/

bool Xdrv129(uint32_t function) {
  bool result = false;
  switch (function) {
    case FUNC_PRE_INIT:
      WMBusOledInit();
      break;
    case FUNC_EVERY_SECOND:
      WMBusOledTick();
      break;
    case FUNC_COMMAND:
      result = DecodeCommand(kWMBusOledCommands, WMBusOledCommand);
      break;
    case FUNC_ACTIVE:
      result = (WOled && WOled->ready);
      break;
  }
  return result;
}

#endif  // USE_I2C
#endif  // USE_WMBUS_OLED

/*
  xdrv_128_wmbus_radio.ino - Wireless M-Bus radio frontend for Tasmota.

  Owns one wM-Bus capable radio (currently SX1262 via RadioLib) and
  delivers received raw payloads to registered decoder callbacks
  (e.g. xsns_121_multical21).

  Architecture
  ------------
  Three-layer split (see lib/lib_rf/wmbus/):

       xsns_121 (decoder + JSON)
              |
              |  WMBusRegisterDecoder(cb)
              v
     +----------------+
     |    xdrv_128    |  <-- this file (driver, IRQ owner, frame queue)
     +-------+--------+
             |  IRadio
             v
     +----------------+        +----------------+
     |  RadioSX1262   |  ...   |  RadioCC1101   |  (post-MVP)
     +----------------+        +----------------+

  Pin selection on Heltec WiFi LoRa 32 V3 (ESP32-S3):

     Function  GPIO  Tasmota slot
     --------  ----  --------------------
     SPI SCK     9   GPIO_SPI_CLK
     SPI MISO   11   GPIO_SPI_MISO
     SPI MOSI   10   GPIO_SPI_MOSI
     LoRa CS     8   GPIO_LORA_CS
     LoRa RST   12   GPIO_LORA_RST
     LoRa BUSY  13   GPIO_LORA_BUSY
     LoRa DIO1  14   GPIO_LORA_DI1

  Build
  -----
     #define USE_SPI
     #define USE_WMBUS_RADIO
     // optional consumer:
     #define USE_MULTICAL21

  Copyright (C) 2026  Tasmota Multical21 fork

  SPDX-License-Identifier: GPL-3.0-only
*/

#ifdef USE_SPI
#ifdef USE_WMBUS_RADIO

#define XDRV_128                       128

#include <SPI.h>
#include "radio_interface.h"
#include "radio_sx1262.h"

/*-------------------------------------------------------------------------------------------*\
 * Public C API (called from xsns_121_multical21.ino and others)
\*-------------------------------------------------------------------------------------------*/

typedef void (*WMBusFrameCb)(const uint8_t *payload, size_t len, int16_t rssi_dbm);

/** Register a decoder callback. Currently a single slot (MVP). */
void WMBusRegisterDecoder(WMBusFrameCb cb);

/** Last RSSI reading in dBm, INT16_MIN if no frame yet. */
int16_t WMBusLastRssi(void);

/** Total frames received from the air (irrespective of CRC / decoder). */
uint32_t WMBusRxCount(void);

/** True if a radio is configured and active. */
bool WMBusRadioReady(void);

/*-------------------------------------------------------------------------------------------*\
 * Logging
\*-------------------------------------------------------------------------------------------*/

#define WMB_LOG(level, fmt, ...) AddLog((level), PSTR("WMB: " fmt), ##__VA_ARGS__)

/*-------------------------------------------------------------------------------------------*\
 * Driver state
\*-------------------------------------------------------------------------------------------*/

#define WMBUS_RX_BUF_MAX  290        // OMS spec maximum frame size

struct WMBusState {
  wmbus::IRadio *radio;
  WMBusFrameCb   decoder_cb;
  uint32_t       rx_count;
  uint32_t       drop_count;
  int16_t        last_rssi;
  bool           radio_ready;   // true once begin()+startRx() succeeded
  uint8_t        init_step;     // 0=ok, else step at which begin() failed
  int16_t        init_status;   // RadioLib status code from the failing step
};

static WMBusState *WMBus = nullptr;

/*-------------------------------------------------------------------------------------------*\
 * Public API implementation
\*-------------------------------------------------------------------------------------------*/

void WMBusRegisterDecoder(WMBusFrameCb cb) {
  if (!WMBus) {
    // xdrv_128 runs FUNC_PRE_INIT before FUNC_INIT of xsns, so WMBus
    // should always exist here. Defensive fallback anyway.
    return;
  }
  WMBus->decoder_cb = cb;
  WMB_LOG(LOG_LEVEL_DEBUG, "decoder registered");
}

int16_t WMBusLastRssi(void)   { return WMBus ? WMBus->last_rssi : INT16_MIN; }
uint32_t WMBusRxCount(void)   { return WMBus ? WMBus->rx_count : 0; }
bool WMBusRadioReady(void)    { return WMBus && WMBus->radio && WMBus->radio_ready; }

/*-------------------------------------------------------------------------------------------*\
 * Radio selection
\*-------------------------------------------------------------------------------------------*/

// Probe configured GPIOs and instantiate the matching radio backend. SX1262
// requires the Tasmota LoRa pin slots to be assigned in the device template.
static wmbus::IRadio *WMBusCreateRadio(void) {
  if (PinUsed(GPIO_LORA_CS) && PinUsed(GPIO_LORA_BUSY)
      && PinUsed(GPIO_LORA_RST) && PinUsed(GPIO_LORA_DI1)) {
    // Heltec WiFi LoRa 32 V3 uses a 1.8 V TCXO and DIO2 as RF switch control.
    // Generic SX1262 modules (e.g. Ebyte E22) typically use a 1.6 V TCXO and
    // no DIO2 switch. For MVP we default to the Heltec V3 configuration.
    return new wmbus::RadioSX1262(
        Pin(GPIO_LORA_CS), Pin(GPIO_LORA_RST),
        Pin(GPIO_LORA_BUSY), Pin(GPIO_LORA_DI1),
        /* tcxo_v   */ 1.8f,
        /* rf switch*/ true);
  }
  return nullptr;
}

/*-------------------------------------------------------------------------------------------*\
 * Init / loop
\*-------------------------------------------------------------------------------------------*/

static void WMBusInit(void) {
  if (WMBus) { return; }
  WMBus = (WMBusState*)calloc(1, sizeof(WMBusState));
  if (!WMBus) { return; }
  WMBus->last_rssi = INT16_MIN;

  if (TasmotaGlobal.spi_enabled != SPI_MOSI_MISO) {
    WMB_LOG(LOG_LEVEL_DEBUG, "SPI not configured (need MOSI+MISO)");
    return;
  }
  // SPI.begin() is idempotent and shared with other Tasmota SPI drivers.
#ifdef ESP8266
  SPI.begin();
#else
  SPI.begin(Pin(GPIO_SPI_CLK), Pin(GPIO_SPI_MISO), Pin(GPIO_SPI_MOSI), -1);
#endif

  WMBus->radio = WMBusCreateRadio();
  if (!WMBus->radio) {
    WMB_LOG(LOG_LEVEL_DEBUG, "no radio pins configured");
    return;
  }
  if (!WMBus->radio->begin()) {
    WMBus->init_step   = WMBus->radio->initStep();
    WMBus->init_status = WMBus->radio->lastStatus();
    WMB_LOG(LOG_LEVEL_INFO, "%s begin() failed step=%u status=%d",
            WMBus->radio->name(), (unsigned)WMBus->init_step, (int)WMBus->init_status);
    // Keep the radio object alive so its diagnostic state survives for
    // WMBusInfo; just leave radio_ready=false.
    return;
  }
  if (!WMBus->radio->startRx()) {
    WMBus->init_status = WMBus->radio->lastStatus();
    WMB_LOG(LOG_LEVEL_INFO, "%s startRx() failed status=%d",
            WMBus->radio->name(), (int)WMBus->init_status);
    return;
  }
  WMBus->radio_ready = true;
  WMB_LOG(LOG_LEVEL_INFO, "%s ready CS=%d RST=%d BUSY=%d DI1=%d freq=868.95 MHz",
          WMBus->radio->name(),
          Pin(GPIO_LORA_CS), Pin(GPIO_LORA_RST),
          Pin(GPIO_LORA_BUSY), Pin(GPIO_LORA_DI1));
}

static void WMBusPoll(void) {
  if (!WMBus || !WMBus->radio || !WMBus->radio_ready) { return; }
  uint8_t  buf[WMBUS_RX_BUF_MAX];
  size_t   len = 0;
  int16_t  rssi = INT16_MIN;
  if (!WMBus->radio->poll(buf, sizeof(buf), &len, &rssi)) { return; }

  WMBus->rx_count++;
  WMBus->last_rssi = rssi;
  WMB_LOG(LOG_LEVEL_DEBUG, "RX %u bytes RSSI=%d", (unsigned)len, (int)rssi);

  if (WMBus->decoder_cb) {
    WMBus->decoder_cb(buf, len, rssi);
  } else {
    WMBus->drop_count++;
  }
}

/*-------------------------------------------------------------------------------------------*\
 * Commands
\*-------------------------------------------------------------------------------------------*/

#define D_PRFX_WMB      "WMBus"
#define D_CMND_WMB_INFO "Info"

const char kWMBusCommands[] PROGMEM = D_PRFX_WMB "|" D_CMND_WMB_INFO;

static void WMBusCmndInfo(void);

void (* const WMBusCommand[])(void) PROGMEM = { &WMBusCmndInfo };

static void WMBusCmndInfo(void) {
  if (!WMBus) { ResponseCmndError(); return; }
  const char *radio_name = "none";
  if (WMBus->radio) {
    radio_name = WMBus->radio_ready ? WMBus->radio->name() : "init-fail";
  }
  Response_P(PSTR("{\"" D_PRFX_WMB D_CMND_WMB_INFO "\":{"
                  "\"Radio\":\"%s\",\"Rx\":%u,\"Dropped\":%u,\"Rssi\":%d,"
                  "\"InitStep\":%u,\"InitStatus\":%d,"
                  "\"Spi\":%d,\"Pins\":{\"CS\":%d,\"RST\":%d,\"BUSY\":%d,\"DI1\":%d,"
                  "\"CLK\":%d,\"MOSI\":%d,\"MISO\":%d}}}"),
             radio_name,
             (unsigned)WMBus->rx_count,
             (unsigned)WMBus->drop_count,
             (int)WMBus->last_rssi,
             (unsigned)WMBus->init_step,
             (int)WMBus->init_status,
             (int)TasmotaGlobal.spi_enabled,
             Pin(GPIO_LORA_CS), Pin(GPIO_LORA_RST),
             Pin(GPIO_LORA_BUSY), Pin(GPIO_LORA_DI1),
             Pin(GPIO_SPI_CLK), Pin(GPIO_SPI_MOSI), Pin(GPIO_SPI_MISO));
}

/*-------------------------------------------------------------------------------------------*\
 * Interface
\*-------------------------------------------------------------------------------------------*/

bool Xdrv128(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_PRE_INIT:
      WMBusInit();
      break;
    case FUNC_EVERY_50_MSECOND:
      WMBusPoll();
      break;
    case FUNC_COMMAND:
      result = DecodeCommand(kWMBusCommands, WMBusCommand);
      break;
    case FUNC_ACTIVE:
      result = (WMBus != nullptr);
      break;
  }
  return result;
}

#endif  // USE_WMBUS_RADIO
#endif  // USE_SPI

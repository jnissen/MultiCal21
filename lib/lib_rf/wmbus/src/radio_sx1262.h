/*
 * radio_sx1262.h - Wireless M-Bus C1 backend on a Semtech SX1262 via RadioLib.
 *
 * Designed for the Heltec WiFi LoRa 32 V3 (ESP32-S3) and pin-compatible
 * boards (LilyGo T3S3, Waveshare SX1262 Node ...). Pins are resolved
 * at runtime from the Tasmota GPIO template via Pin(GPIO_LORA_*).
 *
 * Currently implements Mode C1 only (868.95 MHz, 100 kbps NRZ).
 * Mode T1 / 3-out-of-6 is on the post-MVP TODO list.
 */
#pragma once

#include "radio_interface.h"

#include <RadioLib.h>

namespace wmbus {

class RadioSX1262 : public IRadio {
 public:
  /**
   * @param cs        GPIO for NSS  (Pin(GPIO_LORA_CS))
   * @param rst       GPIO for RESET
   * @param busy      GPIO for BUSY
   * @param dio1      GPIO for DIO1 (interrupt)
   * @param tcxo_v    TCXO voltage. Heltec V3 = 1.8 V, generic boards = 1.6 V.
   * @param dio2_rfsw true if DIO2 controls the on-board RF switch (Heltec V3).
   */
  RadioSX1262(int8_t cs, int8_t rst, int8_t busy, int8_t dio1,
              float tcxo_v = 1.8f, bool dio2_rfsw = true);
  ~RadioSX1262() override;

  bool begin() override;
  bool startRx() override;
  bool poll(uint8_t *out, size_t max_len, size_t *len, int16_t *rssi_dbm) override;
  const char *name() const override { return "SX1262"; }
  int16_t lastStatus() const override { return last_status_; }
  uint8_t initStep() const override { return init_step_; }

 private:
  // Singleton-style trampoline: RadioLib's setDio1Action takes a plain
  // function pointer with no user-data. We therefore allow only one
  // RadioSX1262 instance at a time (sufficient for Tasmota's xdrv_128).
  static void IRAM_ATTR onDioIsr();
  static RadioSX1262 *s_instance;
  static volatile bool s_packet_flag;

  int8_t pin_cs_;
  int8_t pin_rst_;
  int8_t pin_busy_;
  int8_t pin_dio1_;
  float  tcxo_v_;
  bool   dio2_rfsw_;
  SX1262 *radio_;
  bool   ready_;
  int16_t last_rssi_;
  int16_t last_status_ = 0;
  uint8_t init_step_   = 0;
};

}  // namespace wmbus

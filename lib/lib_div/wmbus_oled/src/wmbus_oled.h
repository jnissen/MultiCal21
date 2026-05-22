// SPDX-License-Identifier: Apache-2.0
//
// Minimal SSD1306 128x64 I²C OLED helper.
// - 1024 byte framebuffer (page-aligned)
// - 5x7 ASCII font (rendered as 6 pixels wide incl. spacing)
// - No external dependencies beyond Wire
// - Designed for the Multical21 / wM-Bus radio HUD on Heltec WiFi LoRa 32 V3
//
// Usage:
//   WmbusOled oled;
//   oled.begin(Wire, 0x3C);   // I2C bus already initialized by Tasmota
//   oled.clear();
//   oled.setCursor(0, 0);
//   oled.print("Hello");
//   oled.display();

#pragma once

#include <Arduino.h>
#include <Wire.h>

class WmbusOled : public Print {
 public:
  static constexpr uint8_t  kWidth   = 128;
  static constexpr uint8_t  kHeight  = 64;
  static constexpr uint8_t  kPages   = kHeight / 8;          // 8 pages of 128 bytes
  static constexpr size_t   kFbSize  = (size_t)kWidth * kPages;
  static constexpr uint8_t  kFontW   = 6;                    // 5 + 1 spacing
  static constexpr uint8_t  kFontH   = 8;
  static constexpr uint8_t  kCols    = kWidth  / kFontW;     // 21 chars per line
  static constexpr uint8_t  kRows    = kHeight / kFontH;     // 8 lines

  WmbusOled();

  // Initialize controller. Returns false if the device does not ACK on the bus.
  // `wire` must already be `begin()`-ed by the caller (Tasmota's I2C subsystem).
  bool begin(TwoWire &wire, uint8_t i2c_addr = 0x3C);

  // Framebuffer ops (no I/O until display() is called).
  void clear();
  void setCursor(uint8_t col, uint8_t row);   // text grid (0..kCols-1, 0..kRows-1)
  void setPixel(uint8_t x, uint8_t y, bool on);
  void drawChar(uint8_t col, uint8_t row, char c, bool invert = false);
  void drawString(uint8_t col, uint8_t row, const char *s, bool invert = false);
  void drawHLine(uint8_t y);                   // horizontal separator across full width

  // Push framebuffer to the panel (~9 ms over standard 100 kHz I²C, ~1 ms @ 400 kHz).
  void display();

  // Contrast/dim (0..255). 0 keeps the panel on but very dim.
  void setContrast(uint8_t value);

  // Power the panel on/off (display register; framebuffer is preserved).
  void powerOn();
  void powerOff();

  // Print interface (auto-wraps at right edge, newline = next text row).
  size_t write(uint8_t c) override;

 private:
  void cmd_(uint8_t c);
  void cmdList_(const uint8_t *list, uint8_t n);

  TwoWire *wire_;
  uint8_t  addr_;
  uint8_t  cursor_col_;
  uint8_t  cursor_row_;
  uint8_t  fb_[kFbSize];
};

/*
 * radio_interface.h - Abstract base for wM-Bus radio backends.
 *
 * Implementations:
 *   - RadioSX1262   (RadioLib, ESP32-S3, Heltec WiFi LoRa 32 V3, ...)
 *   - RadioCC1101   (TI CC1101 raw SPI, ESP8266 / ESP32; legacy path
 *                    currently still lives inline in xsns_121_multical21.ino)
 *
 * A backend delivers raw wM-Bus C1 payloads *without* the leading L-field:
 * the first byte of @p out is the C-field, just like the existing
 * xsns_121 M21HandleFrame(length, payload) contract.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace wmbus {

class IRadio {
 public:
  virtual ~IRadio() = default;

  /** Initialise SPI + chip, configure C1 (868.95 MHz, 100 kbps NRZ).
   *  @return true on success. */
  virtual bool begin() = 0;

  /** Arm receive mode (non-blocking). */
  virtual bool startRx() = 0;

  /** Drain one pending packet (if any).
   *  @param out      caller-provided buffer (>= 290 bytes recommended).
   *  @param max_len  capacity of @p out in bytes.
   *  @param[out] len bytes written.
   *  @param[out] rssi_dbm last RSSI in dBm, or INT16_MIN if unknown.
   *  @return true if a frame was returned, false otherwise.
   *
   *  Must be safe to call frequently (e.g. every 50 ms) from FUNC_LOOP. */
  virtual bool poll(uint8_t *out, size_t max_len, size_t *len, int16_t *rssi_dbm) = 0;

  /** Human-readable backend name (compile-time string, for logging). */
  virtual const char *name() const = 0;

  /** Last RadioLib status code from begin()/startRx()/poll(). 0 == OK.
   *  Useful to diagnose silent init failures. */
  virtual int16_t lastStatus() const { return 0; }

  /** Step at which begin() failed (0 == ok, 1 == ctor, 2 == beginFSK,
   *  3 == setEncoding, 4 == setCRC, 5 == setSyncWord, 6 == varLenMode,
   *  7 == startReceive). 0 if all good. */
  virtual uint8_t initStep() const { return 0; }
};

}  // namespace wmbus

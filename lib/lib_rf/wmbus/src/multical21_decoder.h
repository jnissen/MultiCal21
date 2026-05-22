/*
 * multical21_decoder.h - Radio-independent Kamstrup Multical21 / FlowIQ 2200
 *                       wM-Bus decoder.
 *
 * The decoder operates purely on byte buffers + an AES-128 key. It has no
 * dependency on Arduino, Tasmota, RadioLib, SPI, or any radio HW. Suitable
 * for unit testing on host.
 *
 * Input frame layout = raw wM-Bus C1 payload as received from the radio
 * (L-field already consumed, starting at the C-field), exactly as
 * delivered by the SX1262/CC1101 radio backends.
 *
 *   payload[0]      C-field
 *   payload[1..2]   Manufacturer ID
 *   payload[3..6]   Meter serial (LSB-first)
 *   payload[7]      Version
 *   payload[8]      Device type
 *   payload[9]      CI-field
 *   payload[10]     Access No
 *   payload[11]     Status
 *   payload[12..15] Config / signature
 *   payload[16..]   Encrypted application layer
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace wmbus {

enum class MeterType : uint8_t {
  Multical21  = 0,
  FlowIq2200  = 1,
};

struct Multical21Result {
  bool     valid;            ///< CRC + parse succeeded
  uint8_t  ci_field;         ///< 0x79 compact, 0x78 long
  uint32_t serial;           ///< meter id (decimal as printed)
  float    total_m3;         ///< current volume reading
  float    target_m3;        ///< last-billing-period reading (Multical21 only)
  int8_t   flow_temp_c;      ///< water temperature
  int8_t   ambient_temp_c;   ///< ambient temperature
  bool     have_target;
  bool     have_temps;
};

/**
 * Decrypt + parse one wM-Bus C1 application layer frame.
 *
 * @param key        16-byte AES-128 key.
 * @param payload    Raw frame as described above.
 * @param length     Number of bytes in @p payload (typically 32..64).
 * @param expect_id  4-byte little-endian meter id to filter on, or nullptr
 *                   to accept any id.
 * @param meter      Multical21 or FlowIq2200 layout.
 * @param out        Result struct (written only if function returns true).
 * @return true on successful CRC + parse.
 *
 * The function allocates no heap. It uses ~256 bytes of stack for the
 * decrypted buffer.
 */
bool Multical21Decode(const uint8_t  key[16],
                      const uint8_t *payload,
                      size_t         length,
                      const uint8_t *expect_id /* may be nullptr */,
                      MeterType      meter,
                      Multical21Result *out);

}  // namespace wmbus

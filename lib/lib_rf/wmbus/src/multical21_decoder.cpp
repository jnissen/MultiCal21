/*
 * multical21_decoder.cpp - see multical21_decoder.h
 *
 * Extracted 1:1 from xsns_121_multical21.ino: AES-CTR keystream layout,
 * compact-frame offsets (0x79: pos 9/13/17/18) and long-frame offsets
 * (0x78: pos 10/16/23/29) match the upstream chester4444 / pthalin
 * reference implementation.
 */
#include "multical21_decoder.h"
#include "tiny_aes.h"
#include "wmbus_crc.h"

#include <string.h>

namespace wmbus {

namespace {

constexpr size_t kFrameMax = 64;  ///< matches xsns_121 M21_FRAME_MAX

void aesCtrDecrypt(const uint8_t key[16],
                   const uint8_t *payload,
                   uint8_t       *buf,
                   size_t         len) {
  // CTR counter = { payload[1..8], payload[10], payload[12..15], 0, 0, 0 }
  uint8_t iv[16];
  memcpy(iv, &payload[1], 8);
  iv[8]  = payload[10];
  iv[9]  = payload[12];
  iv[10] = payload[13];
  iv[11] = payload[14];
  iv[12] = payload[15];
  iv[13] = 0;
  iv[14] = 0;
  iv[15] = 0;

  uint8_t rk[176];
  TinyAesKeyExpand(key, rk);

  uint8_t ks[16];
  for (size_t off = 0; off < len; off += 16) {
    memcpy(ks, iv, 16);
    TinyAesEncryptBlock(ks, rk);
    size_t chunk = (len - off) < 16 ? (len - off) : 16;
    for (size_t i = 0; i < chunk; i++) {
      buf[off + i] ^= ks[i];
    }
    // Big-endian 32-bit counter increment at iv[12..15]
    for (int i = 15; i >= 12; i--) {
      if (++iv[i]) { break; }
    }
  }
}

bool parsePlain(const uint8_t *data,
                size_t         len,
                MeterType      meter,
                Multical21Result *out) {
  if (len < 7) { return false; }

  uint16_t calc = WmbusCrc16(data + 2, len - 2);
  uint16_t recv = (uint16_t)((uint16_t)data[1] << 8) | data[0];
  if (calc != recv) { return false; }

  int  pos_tt;
  int  pos_tg = -1, pos_ft = -1, pos_at = -1;
  bool have_extra = true;

  if (meter == MeterType::FlowIq2200) {
    if (data[2] != 0x79) { return false; }
    pos_tt    = 29;
    have_extra = false;
  } else if (data[2] == 0x79) {                  // Multical21 compact
    pos_tt = 9;  pos_tg = 13; pos_ft = 17; pos_at = 18;
  } else if (data[2] == 0x78) {                  // Multical21 long
    pos_tt = 10; pos_tg = 16; pos_ft = 23; pos_at = 29;
  } else {
    return false;
  }
  if ((size_t)(pos_tt + 4) > len) { return false; }

  out->ci_field = data[2];

  uint32_t tt = (uint32_t)data[pos_tt]
              | ((uint32_t)data[pos_tt + 1] << 8)
              | ((uint32_t)data[pos_tt + 2] << 16)
              | ((uint32_t)data[pos_tt + 3] << 24);
  out->total_m3 = (float)tt / 1000.0f;

  if (have_extra && pos_tg >= 0 && (size_t)(pos_tg + 4) <= len) {
    uint32_t tg = (uint32_t)data[pos_tg]
                | ((uint32_t)data[pos_tg + 1] << 8)
                | ((uint32_t)data[pos_tg + 2] << 16)
                | ((uint32_t)data[pos_tg + 3] << 24);
    out->target_m3   = (float)tg / 1000.0f;
    out->have_target = true;
  }
  if (have_extra && pos_ft >= 0 && (size_t)pos_ft < len) {
    out->flow_temp_c = (int8_t)data[pos_ft];
    out->have_temps  = true;
  }
  if (have_extra && pos_at >= 0 && (size_t)pos_at < len) {
    out->ambient_temp_c = (int8_t)data[pos_at];
  }
  return true;
}

}  // namespace

bool Multical21Decode(const uint8_t  key[16],
                      const uint8_t *payload,
                      size_t         length,
                      const uint8_t *expect_id,
                      MeterType      meter,
                      Multical21Result *out) {
  if (!key || !payload || !out) { return false; }
  if (length < 18) { return false; }

  // Reset output
  memset(out, 0, sizeof(*out));

  // Optional meter-id filter. Layout in payload: bytes 3..6 = id LSB..MSB.
  // expect_id is also LSB..MSB to match the existing xsns_121 storage order.
  if (expect_id) {
    for (uint8_t i = 0; i < 4; i++) {
      if (expect_id[i] != payload[6 - i]) { return false; }
    }
  }
  out->serial = (uint32_t)payload[3]
              | ((uint32_t)payload[4] << 8)
              | ((uint32_t)payload[5] << 16)
              | ((uint32_t)payload[6] << 24);

  size_t cipher_len = length - 2 - 16;
  if (cipher_len == 0 || cipher_len > kFrameMax) { return false; }

  uint8_t buf[kFrameMax];
  memcpy(buf, &payload[16], cipher_len);
  aesCtrDecrypt(key, payload, buf, cipher_len);

  if (!parsePlain(buf, cipher_len, meter, out)) { return false; }
  out->valid = true;
  return true;
}

}  // namespace wmbus

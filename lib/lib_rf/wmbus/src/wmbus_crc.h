/*
 * wmbus_crc.h - EN13757-3 CRC-16 (poly 0x3D65) used by Wireless M-Bus.
 *
 * Radio-agnostic. Safe to call from any context.
 * Extracted from xsns_121_multical21.ino (M21Crc16).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** EN13757 CRC-16, poly 0x3D65, init 0x0000, final XOR 0xFFFF (one's complement). */
uint16_t WmbusCrc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

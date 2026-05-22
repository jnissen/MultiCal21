/*
 * wmbus_crc.cpp - see wmbus_crc.h
 */
#include "wmbus_crc.h"

#define WMBUS_CRC_POLY 0x3D65

uint16_t WmbusCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0x0000;
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    for (uint8_t j = 0; j < 8; j++) {
      bool xorflag = ((crc & 0x8000) >> 8) ^ (b & 0x80);
      crc = xorflag ? ((crc << 1) ^ WMBUS_CRC_POLY) : (crc << 1);
      b = (uint8_t)(b << 1);
    }
  }
  return (uint16_t)~crc;
}

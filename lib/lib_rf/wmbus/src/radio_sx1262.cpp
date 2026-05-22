/*
 * radio_sx1262.cpp - see radio_sx1262.h
 *
 * Wireless M-Bus Mode C1 reception parameters
 * -------------------------------------------
 *   centre frequency .... 868.950 MHz
 *   modulation .......... 2-GFSK / 2-FSK
 *   bit rate ............ 100 kbps (NRZ encoding)
 *   frequency deviation . ±50 kHz
 *   rx bandwidth ........ 270 kHz (closest RadioLib step above 200 kHz)
 *   preamble ............ 0x55 0x55 ...
 *   sync word ........... 0x54 0x3D       (Other-direction, "meter -> ..")
 *   payload framing ..... L-field followed by C, M, A, version, type, CI, ...
 *
 * The radio is configured in variable-length packet mode. Empirically, the
 * SX1262's FSK sync detector only consumes the first byte of the 2-byte
 * wM-Bus sync 0x54|0x3D, and RadioLib's variable-length handler does not
 * strip the L-field either. poll() therefore removes those two prefix bytes
 * before delivering the buffer so it starts at the C-field, matching the
 * contract of IRadio::poll().
 */
#include "radio_sx1262.h"

#include <string.h>

namespace wmbus {

RadioSX1262 *RadioSX1262::s_instance     = nullptr;
volatile bool RadioSX1262::s_packet_flag = false;

void IRAM_ATTR RadioSX1262::onDioIsr() {
  s_packet_flag = true;
}

RadioSX1262::RadioSX1262(int8_t cs, int8_t rst, int8_t busy, int8_t dio1,
                         float tcxo_v, bool dio2_rfsw)
  : pin_cs_(cs), pin_rst_(rst), pin_busy_(busy), pin_dio1_(dio1),
    tcxo_v_(tcxo_v), dio2_rfsw_(dio2_rfsw),
    radio_(nullptr), ready_(false), last_rssi_(INT16_MIN) {
  s_instance = this;
}

RadioSX1262::~RadioSX1262() {
  if (radio_) {
    radio_->sleep();
    delete radio_;
    radio_ = nullptr;
  }
  if (s_instance == this) { s_instance = nullptr; }
}

bool RadioSX1262::begin() {
  if (ready_) { return true; }
  init_step_   = 1;
  last_status_ = 0;
  if (pin_cs_ < 0 || pin_rst_ < 0 || pin_busy_ < 0 || pin_dio1_ < 0) {
    return false;
  }

  // RadioLib's Module(cs, irq, rst, busy). The default constructor binds to
  // the global Arduino `SPI` instance; SPI.begin() is already called by the
  // xdrv driver before begin().
  radio_ = new SX1262(new Module(pin_cs_, pin_dio1_, pin_rst_, pin_busy_));
  if (!radio_) { return false; }

  init_step_ = 2;
  // beginFSK(freq, br, freqDev, rxBw, power, preambleLength, tcxoVoltage)
  // NOTE: SX1262 accepts only discrete RX bandwidths. Valid steps around
  // the wM-Bus C1 requirement (Carson ~200 kHz): 187.2, 234.3, 312.0 kHz.
  // 234.3 kHz is the closest >=200 kHz step. (270 kHz -> ERR_INVALID_RX_BANDWIDTH -104.)
  last_status_ = radio_->beginFSK(
      /* freq         */ 868.95f,
      /* br kbps      */ 100.0f,
      /* freqDev kHz  */ 50.0f,
      /* rxBw kHz     */ 234.3f,
      /* power dBm    */ 10,        // RX-only, value irrelevant
      /* preambleLen  */ 16,
      /* tcxo V       */ tcxo_v_);
  if (last_status_ != RADIOLIB_ERR_NONE) { return false; }

  if (dio2_rfsw_) {
    radio_->setDio2AsRfSwitch(true);   // Heltec V3 RF switch is on DIO2
  }

  init_step_ = 3;
  // Mode C1 = NRZ. Manchester only applies to T1 (post-MVP).
  last_status_ = radio_->setEncoding(RADIOLIB_ENCODING_NRZ);
  if (last_status_ != RADIOLIB_ERR_NONE) { return false; }

  init_step_ = 4;
  // wM-Bus has its own CRC layer (EN13757-3, poly 0x3D65) which is
  // checked in software by multical21_decoder. Disable the SX1262's
  // built-in CRC so it does not drop our packets.
  last_status_ = radio_->setCRC(0);
  if (last_status_ != RADIOLIB_ERR_NONE) { return false; }

  init_step_ = 5;
  // 2-byte sync word 0x54 0x3D (OMS mode C1 other-direction).
  uint8_t sync[2] = { 0x54, 0x3D };
  last_status_ = radio_->setSyncWord(sync, sizeof(sync));
  if (last_status_ != RADIOLIB_ERR_NONE) { return false; }

  init_step_ = 6;
  // Variable length, max ~290 bytes. The L-field acts as length byte.
  last_status_ = radio_->variablePacketLengthMode(255);  // RadioLib limit; sufficient for Multical21
  if (last_status_ != RADIOLIB_ERR_NONE) { return false; }

  radio_->setDio1Action(&RadioSX1262::onDioIsr);

  init_step_   = 0;
  last_status_ = 0;
  ready_       = true;
  return true;
}

bool RadioSX1262::startRx() {
  if (!ready_) { return false; }
  s_packet_flag = false;
  return radio_->startReceive() == RADIOLIB_ERR_NONE;
}

bool RadioSX1262::poll(uint8_t *out, size_t max_len, size_t *len, int16_t *rssi_dbm) {
  if (!ready_ || !out || !len) { return false; }
  if (!s_packet_flag) { return false; }
  s_packet_flag = false;

  size_t plen = radio_->getPacketLength();
  if (plen == 0 || plen > max_len) {
    radio_->startReceive();             // re-arm and drop frame
    return false;
  }

  int16_t st = radio_->readData(out, plen);
  // CRC errors from the radio are expected when running with setCRC(0);
  // we still get the bytes. Anything else is a hard read failure.
  if (st != RADIOLIB_ERR_NONE && st != RADIOLIB_ERR_CRC_MISMATCH) {
    radio_->startReceive();
    return false;
  }

  last_rssi_ = (int16_t)radio_->getRSSI();
  if (rssi_dbm) { *rssi_dbm = last_rssi_; }

  // Drop the residual 2nd sync byte (0x3D) plus the wM-Bus L-field so the
  // buffer starts at the C-field as the decoder expects. If the leading byte
  // is not 0x3D, the frame is unsynchronised garbage -> drop it.
  if (plen < 3 || out[0] != 0x3D) {
    radio_->startReceive();
    return false;
  }
  memmove(out, out + 2, plen - 2);
  plen -= 2;
  *len = plen;

  radio_->startReceive();               // immediately re-arm
  return true;
}

}  // namespace wmbus

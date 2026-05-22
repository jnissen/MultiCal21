// SPDX-License-Identifier: Apache-2.0
//
// Shared snapshot struct exposed by xsns_121_multical21.ino and consumed by
// xdrv_129_wmbus_display.ino (OLED HUD). Lives in a header because Tasmota
// concatenates all .ino files into a single translation unit, so an in-line
// typedef in two .ino files collides.

#pragma once

#include <stdint.h>

typedef struct {
  bool     have_data;
  bool     configured;
  float    total_m3;
  float    target_m3;
  int8_t   flow_temp_c;
  int8_t   ambient_temp_c;
  int8_t   last_rssi_dbm;
  uint32_t frames_valid;
  uint32_t frames_total;
  uint32_t last_valid_ms;
} M21DisplaySnapshot;

// Implemented in xsns_121_multical21.ino. The argument is typed as `void *`
// so the Arduino auto-prototype generator (which inserts a forward declaration
// near the top of the concatenated translation unit, before any user includes)
// does not need to know the M21DisplaySnapshot struct. Callers should pass a
// pointer to an M21DisplaySnapshot.
bool M21GetDisplaySnapshot(void *out);

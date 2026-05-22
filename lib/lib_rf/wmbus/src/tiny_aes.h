/*
 * tiny_aes.h - Minimal AES-128 ECB block primitive + 11-round key expansion.
 *
 * Public domain (derived from tiny-AES-c by Kokke). Self-contained:
 * no dependency on Arduino, mbedTLS or BearSSL so the wM-Bus stack
 * builds in any Tasmota environment.
 *
 * Extracted from xsns_121_multical21.ino.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Expand 16-byte AES-128 key into 11 round keys (176 bytes). */
void TinyAesKeyExpand(const uint8_t key[16], uint8_t round_keys[176]);

/** Encrypt one 16-byte block in place using a pre-expanded key schedule. */
void TinyAesEncryptBlock(uint8_t block[16], const uint8_t round_keys[176]);

#ifdef __cplusplus
}
#endif

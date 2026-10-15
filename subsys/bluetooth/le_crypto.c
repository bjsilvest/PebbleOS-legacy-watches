/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/bluetooth/le_crypto.h>

#include <mbedtls/aes.h>

bool pbl_bt_le_e(const uint8_t key[16], const uint8_t plain[16], uint8_t out[16]) {
  // AES works on big-endian (most significant byte first) blocks.
  uint8_t key_be[16];
  uint8_t plain_be[16];
  uint8_t out_be[16];
  for (int i = 0; i < 16; i++) {
    key_be[i] = key[15 - i];
    plain_be[i] = plain[15 - i];
  }

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  const bool ok = mbedtls_aes_setkey_enc(&ctx, key_be, 128) == 0 &&
                  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, plain_be, out_be) == 0;
  mbedtls_aes_free(&ctx);

  for (int i = 0; i < 16; i++) {
    out[i] = out_be[15 - i];
  }
  return ok;
}

bool pbl_bt_le_irk_resolves(const uint8_t irk[16], const uint8_t rpa[6]) {
  // ah(k, r) = e(k, padding || r) mod 2^24, r being prand (the top 24 bits).
  uint8_t plain[16] = {0};
  uint8_t out[16];
  memcpy(plain, &rpa[3], 3);
  if (!pbl_bt_le_e(irk, plain, out)) {
    return false;
  }
  return memcmp(out, rpa, 3) == 0;
}

bool pbl_bt_le_d1(const uint8_t k[16], uint16_t d, uint16_t r, uint8_t out[16]) {
  // d' = padding || r || d
  uint8_t plain[16] = {0};
  plain[0] = d & 0xffU;
  plain[1] = d >> 8;
  plain[2] = r & 0xffU;
  plain[3] = r >> 8;
  return pbl_bt_le_e(k, plain, out);
}

bool pbl_bt_le_dm(const uint8_t k[16], uint64_t r, uint16_t *out) {
  // dm(k, r) = e(k, padding || r) mod 2^16
  uint8_t plain[16] = {0};
  uint8_t enc[16];
  for (int i = 0; i < 8; i++) {
    plain[i] = (r >> (8 * i)) & 0xffU;
  }
  if (!pbl_bt_le_e(k, plain, enc)) {
    return false;
  }
  *out = enc[0] | (enc[1] << 8);
  return true;
}

/*
 * SPDX-FileCopyrightText: 2026 Brian Silvestri
 * SPDX-License-Identifier: Apache-2.0
 */

/* Smaller LE Secure Connections crypto for tiny boards: micro-ecc instead of
 * the Mbed TLS bignum/ECP code and a CMAC on top of the AES already linked.
 * ble_sm_alg.c gets its own versions renamed away, see CMakeLists.txt.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/cipher.h>

#include "host/ble_hs.h"
#include "host/ble_hs_hci.h"
#include "os/endian.h"
#include "uECC.h"

#define AES_BLOCK 16
#define P256_COORD 32

static int prv_rng(uint8_t *dest, unsigned size) {
  return ble_hs_hci_rand(dest, (int)size) == 0;
}

int ble_sm_alg_gen_key_pair(uint8_t *pub, uint8_t *priv) {
  uint8_t pub_be[2 * P256_COORD];
  uint8_t priv_be[P256_COORD];

  uECC_set_rng(prv_rng);
  if (!uECC_make_key(pub_be, priv_be, uECC_secp256r1())) {
    return BLE_HS_EUNKNOWN;
  }

  swap_buf(pub, pub_be, P256_COORD);
  swap_buf(pub + P256_COORD, pub_be + P256_COORD, P256_COORD);
  swap_buf(priv, priv_be, P256_COORD);
  return 0;
}

int ble_sm_alg_gen_dhkey(const uint8_t *peer_pub_key_x, const uint8_t *peer_pub_key_y,
                         const uint8_t *our_priv_key, uint8_t *out_dhkey) {
  const uECC_Curve curve = uECC_secp256r1();
  uint8_t pub_be[2 * P256_COORD];
  uint8_t priv_be[P256_COORD];
  uint8_t secret_be[P256_COORD];

  swap_buf(pub_be, peer_pub_key_x, P256_COORD);
  swap_buf(pub_be + P256_COORD, peer_pub_key_y, P256_COORD);
  swap_buf(priv_be, our_priv_key, P256_COORD);

  uECC_set_rng(prv_rng);
  // The peer's point must be on the curve (CVE-2018-5383)
  if (!uECC_valid_public_key(pub_be, curve) ||
      !uECC_shared_secret(pub_be, priv_be, secret_be, curve)) {
    return BLE_HS_EUNKNOWN;
  }

  swap_buf(out_dhkey, secret_be, P256_COORD);
  return 0;
}

const mbedtls_cipher_info_t *ble_sm_port_cipher_info(mbedtls_cipher_type_t type) {
  (void)type;
  return NULL;
}

static void prv_cmac_double(uint8_t block[AES_BLOCK]) {
  const uint8_t carry = block[0] >> 7;
  for (int i = 0; i < AES_BLOCK - 1; i++) {
    block[i] = (uint8_t)(block[i] << 1) | (block[i + 1] >> 7);
  }
  block[AES_BLOCK - 1] = (uint8_t)(block[AES_BLOCK - 1] << 1) ^ (carry ? 0x87 : 0);
}

static void prv_xor(uint8_t *dst, const uint8_t *src, size_t len) {
  for (size_t i = 0; i < len; i++) {
    dst[i] ^= src[i];
  }
}

/* RFC 4493 AES-CMAC, standing in for mbedtls_cipher_cmac() */
int ble_sm_port_cmac(const mbedtls_cipher_info_t *info, const unsigned char *key, size_t keybits,
                     const unsigned char *in, size_t len, unsigned char *out) {
  (void)info;
  mbedtls_aes_context ctx;
  uint8_t subkey[AES_BLOCK] = {0};
  uint8_t state[AES_BLOCK] = {0};
  int err;

  mbedtls_aes_init(&ctx);
  err = mbedtls_aes_setkey_enc(&ctx, key, (unsigned int)keybits);
  if (err == 0) {
    err = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, subkey, subkey);
  }
  prv_cmac_double(subkey);  // K1

  while (err == 0 && len > AES_BLOCK) {
    prv_xor(state, in, AES_BLOCK);
    err = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, state, state);
    in += AES_BLOCK;
    len -= AES_BLOCK;
  }

  if (err == 0) {
    prv_xor(state, in, len);
    if (len < AES_BLOCK) {
      state[len] ^= 0x80;
      prv_cmac_double(subkey);  // K2
    }
    prv_xor(state, subkey, AES_BLOCK);
    err = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, state, out);
  }

  mbedtls_aes_free(&ctx);
  return err;
}

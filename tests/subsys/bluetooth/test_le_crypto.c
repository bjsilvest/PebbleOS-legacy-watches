/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <string.h>

#include <pbl/bluetooth/le_crypto.h>

#include <clar.h>

// mbedtls's Pebble configuration frees through the kernel heap.
void kernel_free(void *ptr) {
  free(ptr);
}

// Values are in HCI (little endian) order: written most significant byte
// first as in the Core spec, then reversed here.
static void prv_from_spec(const uint8_t *spec, size_t len, uint8_t *out) {
  for (size_t i = 0; i < len; i++) {
    out[i] = spec[len - 1 - i];
  }
}

void test_le_crypto__ah_spec_sample(void) {
  // Core spec Vol 3 Part H D.7: IRK ec0234a3..., prand 708194, hash 0dfbaa.
  static const uint8_t irk_spec[16] = {0xec, 0x02, 0x34, 0xa3, 0x57, 0xc8, 0xad, 0x05,
                                       0x34, 0x10, 0x10, 0xa6, 0x0a, 0x39, 0x7d, 0x9b};
  static const uint8_t rpa_spec[6] = {0x70, 0x81, 0x94, 0x0d, 0xfb, 0xaa};
  uint8_t irk[16];
  uint8_t rpa[6];
  prv_from_spec(irk_spec, sizeof(irk_spec), irk);
  prv_from_spec(rpa_spec, sizeof(rpa_spec), rpa);

  cl_assert(pbl_bt_le_irk_resolves(irk, rpa));

  rpa[0] ^= 1;
  cl_assert(!pbl_bt_le_irk_resolves(irk, rpa));
}

void test_le_crypto__e_matches_ah(void) {
  // ah() is e() truncated: the hash is the low 24 bits of e(IRK, prand).
  static const uint8_t irk_spec[16] = {0xec, 0x02, 0x34, 0xa3, 0x57, 0xc8, 0xad, 0x05,
                                       0x34, 0x10, 0x10, 0xa6, 0x0a, 0x39, 0x7d, 0x9b};
  uint8_t irk[16];
  prv_from_spec(irk_spec, sizeof(irk_spec), irk);
  uint8_t plain[16] = {0x94, 0x81, 0x70};
  uint8_t out[16];
  cl_assert(pbl_bt_le_e(irk, plain, out));
  cl_assert_equal_i(out[0], 0xaa);
  cl_assert_equal_i(out[1], 0xfb);
  cl_assert_equal_i(out[2], 0x0d);
}

void test_le_crypto__d1_dm_are_e(void) {
  uint8_t k[16];
  for (int i = 0; i < 16; i++) {
    k[i] = (uint8_t)(i * 7 + 1);
  }

  // d1(k, d, r) = e(k, padding || r || d)
  uint8_t d1[16];
  uint8_t plain[16] = {0x34, 0x12, 0x78, 0x56};
  uint8_t expected[16];
  cl_assert(pbl_bt_le_d1(k, 0x1234, 0x5678, d1));
  cl_assert(pbl_bt_le_e(k, plain, expected));
  cl_assert_equal_m(d1, expected, sizeof(d1));

  // dm(k, r) = e(k, padding || r) mod 2^16
  uint16_t dm;
  uint8_t rplain[16] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
  cl_assert(pbl_bt_le_dm(k, 0x0102030405060708ULL, &dm));
  cl_assert(pbl_bt_le_e(k, rplain, expected));
  cl_assert_equal_i(dm, expected[0] | (expected[1] << 8));

  // Stock (Bluetopia) dm keeps the top 16 bits instead
  cl_assert(pbl_bt_le_dm_stock(k, 0x0102030405060708ULL, &dm));
  cl_assert_equal_i(dm, expected[14] | (expected[15] << 8));
}

void test_le_crypto__legacy_ediv_roundtrip(void) {
  // Legacy key generation (Vol 3 Part H Appendix B): DHK = d1(IR, 3, 0),
  // EDIV = DIV ^ dm(DHK, Rand). The transport recovers DIV the same way.
  uint8_t ir[16];
  for (int i = 0; i < 16; i++) {
    ir[i] = (uint8_t)(0xa5 ^ (i * 13));
  }
  const uint16_t div = 0xbeef;
  const uint64_t rand = 0x1122334455667788ULL;

  uint8_t dhk[16];
  uint16_t y;
  cl_assert(pbl_bt_le_d1(ir, 3, 0, dhk));
  cl_assert(pbl_bt_le_dm(dhk, rand, &y));
  const uint16_t ediv = div ^ y;

  uint16_t y2;
  cl_assert(pbl_bt_le_dm(dhk, rand, &y2));
  cl_assert_equal_i((uint16_t)(ediv ^ y2), div);
  cl_assert(pbl_bt_le_dm(dhk, rand + 1, &y2));
  cl_assert((uint16_t)(ediv ^ y2) != div);
}

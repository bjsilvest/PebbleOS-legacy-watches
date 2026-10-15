/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @defgroup bluetooth_le_crypto LE key functions
 * @ingroup bluetooth
 * @brief Bluetooth Core Vol 3 Part H security functions, on keys and values
 * in HCI (little endian) byte order.
 * @{
 */

/**
 * @brief e(key, plain): AES-128, as the controller's LE Encrypt.
 * @return false if AES failed.
 */
bool pbl_bt_le_e(const uint8_t key[16], const uint8_t plain[16], uint8_t out[16]);

/** @brief Whether @p irk resolves the resolvable private address @p rpa (ah()). */
bool pbl_bt_le_irk_resolves(const uint8_t irk[16], const uint8_t rpa[6]);

/** @brief d1(k, d, r), the diversifying function of legacy pairing key generation. */
bool pbl_bt_le_d1(const uint8_t k[16], uint16_t d, uint16_t r, uint8_t out[16]);

/** @brief dm(k, r), the EDIV mask function of legacy pairing key generation. */
bool pbl_bt_le_dm(const uint8_t k[16], uint64_t r, uint16_t *out);

/** @} */

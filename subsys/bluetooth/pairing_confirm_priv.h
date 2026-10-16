/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

//! The connection whose pairing shows a passkey NimBLE already has (legacy
//! passkey entry, typed on the phone), or BLE_HS_CONN_HANDLE_NONE.
void pairing_confirm_set_display_conn(uint16_t conn_handle);

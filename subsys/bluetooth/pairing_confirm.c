/* SPDX-FileCopyrightText: 2025 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pairing_confirm_priv.h"

#include <stdint.h>

#include <pbl/bluetooth/pairing_confirm.h>
#include <pbl/logging/logging.h>

#include <host/ble_hs.h>
#include <host/ble_sm.h>

PBL_LOG_MODULE_DECLARE(bt, CONFIG_BT_LOG_LEVEL);

static uint16_t s_display_conn = BLE_HS_CONN_HANDLE_NONE;

void pairing_confirm_set_display_conn(uint16_t conn_handle) {
  s_display_conn = conn_handle;
}

void pbl_bt_pairing_confirm(const struct pbl_bt_pairing_confirm_ctx *ctx, bool is_confirmed) {
  uint16_t conn_handle = (uintptr_t)ctx;
  if (conn_handle == s_display_conn) {
    // NimBLE has the passkey already; what the user types on the phone
    // decides. Declining on the watch ends the attempt.
    s_display_conn = BLE_HS_CONN_HANDLE_NONE;
    if (!is_confirmed) {
      (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return;
  }

  struct ble_sm_io key = {
    .action = BLE_SM_IOACT_NUMCMP,
    .numcmp_accept = is_confirmed,
  };
  int rc = ble_sm_inject_io(conn_handle, &key);

  PBL_LOG_DBG("ble_sm_inject_io rc=0x%04x", (uint16_t)rc);
}

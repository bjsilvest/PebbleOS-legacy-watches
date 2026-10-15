/* SPDX-FileCopyrightText: 2025 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "id_priv.h"

#include <pbl/bluetooth/id.h>
#if defined(CONFIG_BT_CONTROLLER_DA1468X) || defined(CONFIG_BT_CONTROLLER_CC2564)
#include <pbl/bluetooth/hci_uart.h>
#include <pbl/bluetooth/types.h>
#include <pbl/logging/logging.h>
#endif

#include <host/ble_hs_id.h>
#include <services/gap/ble_svc_gap.h>
#include <system/passert.h>

#if defined(CONFIG_BT_CONTROLLER_DA1468X) || defined(CONFIG_BT_CONTROLLER_CC2564)
PBL_LOG_MODULE_DECLARE(bt, CONFIG_BT_LOG_LEVEL);
#endif

void pbl_bt_id_set_local_device_name(const char device_name[PBL_BT_DEVICE_NAME_BUFFER_SIZE]) {
  int rc = ble_svc_gap_device_name_set(device_name);
  PBL_ASSERTN(rc == 0);
}

void pbl_bt_id_copy_local_identity_address(struct pbl_bt_addr *addr_out) {
  int rc;
  uint8_t own_addr_type;

  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  PBL_ASSERTN(rc == 0);

  rc = ble_hs_id_copy_addr(own_addr_type, (uint8_t *)&addr_out->octets, nullptr);
  PBL_ASSERTN(rc == 0);
}

void pbl_bt_set_local_address(bool allow_cycling, const struct pbl_bt_addr *pinned_address) {
#if defined(CONFIG_BT_CONTROLLER_DA1468X) || defined(CONFIG_BT_CONTROLLER_CC2564)
  // Only the stock firmware's pinned address (imported from its shared PRF storage) is a
  // resolvable private one; the generated placeholder is all zeros.
  const bool use_pinned =
      !allow_cycling && pinned_address != nullptr && (pinned_address->octets[5] & 0xc0U) == 0x40U;
  pbl_bt_hci_uart_set_local_rpa(use_pinned ? pinned_address->octets : nullptr);
  if (pbl_bt_id_sync_local_address() && use_pinned) {
    PBL_LOG_INFO("Using pinned address " PBL_BT_ADDR_FMT, PBL_BT_ADDR_XPLODE_PTR(pinned_address));
  }
#endif
}

bool pbl_bt_id_sync_local_address(void) {
#if defined(CONFIG_BT_CONTROLLER_DA1468X) || defined(CONFIG_BT_CONTROLLER_CC2564)
  if (!pbl_bt_hci_uart_local_addr_outdated()) {
    return true;
  }

  // Re-send the random address: the transport puts the chosen one in. The controller refuses
  // it while advertising, so advertising retries this before it starts.
  uint8_t id_addr[6];
  int rc = ble_hs_id_copy_addr(BLE_ADDR_RANDOM, id_addr, nullptr);
  if (rc == 0) {
    rc = ble_hs_id_set_rnd(id_addr);
  }
  if (rc != 0) {
    PBL_LOG_WRN("Could not apply the local address (0x%x)", rc);
    return false;
  }
#endif
  return true;
}

void pbl_bt_id_copy_chip_info_string(char *dest, size_t dest_size) {
  strncpy(dest, "NimBLE", dest_size);
}

bool pbl_bt_id_generate_private_resolvable_address(struct pbl_bt_addr *address_out) {
  *address_out = (struct pbl_bt_addr){};
  return true;
}

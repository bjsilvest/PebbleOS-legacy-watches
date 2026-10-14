/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/bluetooth/id_addr.h>
#include <pbl/services/bluetooth/local_id.h>

int pbl_bt_id_addr_get(struct pbl_bt_addr *addr, enum pbl_bt_id_addr_type *type) {
  // Same derivation as the legacy firmware, so the address never changes.
  bt_local_id_generate_address_from_serial(addr);
  *type = PBL_BT_ID_ADDR_RANDOM_STATIC;

  return 0;
}

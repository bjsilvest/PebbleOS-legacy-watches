/* SPDX-FileCopyrightText: 2025 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "nimble_gattc_op_queue.h"
#include "nimble_type_conversions.h"

#include <pbl/bluetooth/advert.h>
#include <pbl/bluetooth/gap_le_device_name.h>
#include <pbl/logging/logging.h>
#include <pbl/util/math.h>
#include <pbl/services/system_task.h>

#include <comm/bt_lock.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <kernel/pbl_malloc.h>
#include <os/os_mbuf.h>

PBL_LOG_MODULE_DECLARE(bt, CONFIG_BT_LOG_LEVEL);

#define GAP_DEVICE_NAME_CHR (0x2A00)

const ble_uuid16_t device_name_chr_uuid = BLE_UUID16_INIT(GAP_DEVICE_NAME_CHR);

static int prv_device_name_read_event_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                         struct ble_gatt_attr *attr, void *arg) {
  const struct pbl_bt_device_internal *device = arg;

  if (error->status != 0) {
    if (error->status != BLE_HS_EDONE) {
      PBL_LOG_ERR("prv_device_name_read_event_cb error=%d", error->status);
      PBL_BT_NAME_TRACE('E');
      PBL_BT_NAME_TRACE("0123456789abcdef"[(error->status >> 8) & 0xf]);
      PBL_BT_NAME_TRACE("0123456789abcdef"[(error->status >> 4) & 0xf]);
      PBL_BT_NAME_TRACE("0123456789abcdef"[error->status & 0xf]);
    } else {
      PBL_BT_NAME_TRACE('.');
    }
    // Frees arg (the op context); must be the last use of it
    nimble_gattc_op_queue_complete();
    return 0;
  }

  const uint16_t name_len = OS_MBUF_PKTLEN(attr->om);
  PBL_BT_NAME_TRACE('N');
  PBL_BT_NAME_TRACE("0123456789abcdef"[MIN(name_len, 15U)]);

  PBL_LOG_DBG("Device name read cb: conn=%u handle=%u len=%u", conn_handle, attr->handle, name_len);

  char *device_name = kernel_zalloc_check(name_len + 1);
  if (ble_hs_mbuf_to_flat(attr->om, device_name, name_len, nullptr) != 0) {
    kernel_free(device_name);
    return 0;
  }

  bool changed;
  struct pbl_bt_addr addr_copy;

  // Pairing can resolve the peer's identity while the read is in flight, which
  // changes the connection's address from the one the request was made for:
  // look the connection up by its current address so the name is kept on the
  // first pairing, not only after reconnecting.
  struct pbl_bt_device_internal current = *device;
  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(conn_handle, &desc) == 0) {
    nimble_addr_to_pebble_device(&desc.peer_id_addr, &current);
  }

  bt_lock();

  GAPLEConnection *connection = gap_le_connection_by_device(&current);
  if (connection == nullptr) {
    PBL_BT_NAME_TRACE('?');
    bt_unlock();
    kernel_free(device_name);
    return 0;
  }

  changed =
      (connection->device_name == nullptr) || strcmp(connection->device_name, device_name) != 0;
  if (changed) {
    if (connection->device_name) {
      kernel_free(connection->device_name);
    }
    connection->device_name = device_name;
  }
  addr_copy = connection->device.address;

  bt_unlock();

  if (!changed) {
    PBL_BT_NAME_TRACE('=');
    // Same name as before: don't re-persist it (each store rewrites the
    // pairing storage and fires bonding change handlers).
    kernel_free(device_name);
    return 0;
  }

  struct pbl_bt_addr *addr = kernel_zalloc_check(sizeof(struct pbl_bt_addr));
  *addr = addr_copy;
  system_task_add_callback(pbl_bt_store_device_name_kernelbg_cb, addr);

  return 0;
}

static int prv_device_name_read_op_start(void *ctx) {
  const struct pbl_bt_device_internal *device = ctx;
  uint16_t conn_handle;

  if (!pebble_device_to_nimble_conn_handle(device, &conn_handle)) {
    PBL_BT_NAME_TRACE('x');
    PBL_LOG_WRN("Device name read: connection is gone");
    return BLE_HS_ENOTCONN;
  }

  PBL_LOG_DBG("Device name read request: conn=%u", conn_handle);

  int rc = ble_gattc_read_by_uuid(conn_handle, 1, UINT16_MAX, (ble_uuid_t *)&device_name_chr_uuid,
                                  prv_device_name_read_event_cb, ctx);
  PBL_BT_NAME_TRACE(rc == 0 ? 'S' : 'x');
  if (rc != 0) {
    PBL_LOG_ERR("prv_device_name_read_op_start ble_gattc_read_by_uuid rc=0x%04x", (uint16_t)rc);
  }

  return rc;
}

void pbl_bt_gap_le_device_name_request(const struct pbl_bt_device_internal *device) {
  PBL_BT_NAME_TRACE('Q');
  struct pbl_bt_device_internal *ctx = kernel_zalloc_check(sizeof(*ctx));
  *ctx = *device;
  nimble_gattc_op_queue_push(prv_device_name_read_op_start, ctx);
}

static void prv_request_device_name_cb(GAPLEConnection *connection, void *data) {
  pbl_bt_gap_le_device_name_request(&connection->device);
}

void pbl_bt_gap_le_device_name_request_all(void) {
  bt_lock();
  gap_le_connection_for_each(prv_request_device_name_cb, nullptr);
  bt_unlock();
}

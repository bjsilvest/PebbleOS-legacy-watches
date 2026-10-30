/* SPDX-FileCopyrightText: 2025 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "id_priv.h"
#include "nimble_gattc_op_queue.h"
#include "nimble_type_conversions.h"
#include "pairability_priv.h"
#include "pairing_confirm_priv.h"

#include <stdio.h>

#include <pbl/bluetooth/advert.h>
#include <pbl/bluetooth/bonding_sync.h>
#include <pbl/bluetooth/gatt.h>
#include <pbl/bluetooth/pairing_confirm.h>
#include <pbl/drivers/rng.h>
#include <pbl/kernel/assert.h>
#include <pbl/logging/logging.h>
#include <pbl/util/math.h>

#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#ifdef CONFIG_BT_OWN_SERVICE_CHANGED
#include <comm/ble/gap_le_connection.h>
#include <comm/ble/gatt_service_changed.h>
#include <comm/bt_conn_mgr.h>
#include <comm/bt_lock.h>
#include <host/ble_uuid.h>
#include <nimble/nimble_port.h>
#include <pbl/crc/crc.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/settings/settings_file.h>
#include <pbl/services/shared_prf_storage/shared_prf_storage.h>
#endif
#include <kernel/pbl_malloc.h>
#include <os/os_mbuf.h>
#include <services/gatt/ble_svc_gatt.h>

PBL_LOG_MODULE_DECLARE(bt, CONFIG_BT_LOG_LEVEL);

static const ble_uuid16_t s_device_name_chr_uuid = BLE_UUID16_INIT(0x2A00);
static char s_device_name[PBL_BT_DEVICE_NAME_BUFFER_SIZE];
static bool s_pairing_in_progress;
static uint16_t s_pairing_conn_handle;

static int prv_device_name_read_event_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                         struct ble_gatt_attr *attr, void *arg) {
  if (error->status != 0) {
    nimble_gattc_op_queue_complete();
    return 0;
  }

  size_t len = MIN(OS_MBUF_PKTLEN(attr->om), sizeof(s_device_name) - 1);
  os_mbuf_copydata(attr->om, 0, len, s_device_name);
  s_device_name[len] = '\0';

  return 0;
}

static int prv_device_name_read_op_start(void *ctx) {
  const uint16_t conn_handle = *(uint16_t *)ctx;

  int rc = ble_gattc_read_by_uuid(conn_handle, 1, UINT16_MAX, (ble_uuid_t *)&s_device_name_chr_uuid,
                                  prv_device_name_read_event_cb, nullptr);
  if (rc != 0) {
    PBL_LOG_ERR("Pairing device name read failed to start (rc=0x%04x)", (uint16_t)rc);
  }

  return rc;
}

void pbl_bt_advert_advertising_disable(void) {
  int rc;

  if (ble_gap_adv_active() == 0) {
    return;
  }

  rc = ble_gap_adv_stop();
  PBL_ASSERT(rc == 0, "Failed to stop advertising (0x%04x)", (uint16_t)rc);
}

bool pbl_bt_advert_client_get_tx_power(int8_t *tx_power) {
  return false;
}

bool pbl_bt_advert_set_advertising_data(const struct pbl_bt_ad_data *ad_data) {
  int rc;

  rc = ble_gap_adv_set_data((uint8_t *)&ad_data->data, ad_data->ad_data_length);
  if (rc != 0) {
    PBL_LOG_ERR("Failed to set advertising data (0x%04x)", (uint16_t)rc);
    return false;
  }

  rc = ble_gap_adv_rsp_set_data((uint8_t *)&ad_data->data[ad_data->ad_data_length],
                                ad_data->scan_resp_data_length);
  if (rc != 0) {
    PBL_LOG_ERR("Failed to set scan response data (0x%04x)", (uint16_t)rc);
    return false;
  }

  return true;
}

#ifdef CONFIG_BT_CONTROLLER_DIAGNOSTICS
// Pairing trace for diagnostics: how far the last pairing got and the
// status codes it ended with.
static struct {
  uint8_t requests;
  uint8_t passkeys;
  uint8_t passkey_action;
  uint16_t pairing_status;
  uint16_t enc_status;
  uint16_t disconnect_reason;
} s_pair_trace;
static char s_pair_trace_str[32];

const char *pbl_bt_advert_pairing_trace(void) {
  sniprintf(s_pair_trace_str, sizeof(s_pair_trace_str), "r%uk%ua%u p%03x e%03x d%03x",
            (unsigned)MIN(s_pair_trace.requests, 9U), (unsigned)MIN(s_pair_trace.passkeys, 9U),
            (unsigned)s_pair_trace.passkey_action, (unsigned)s_pair_trace.pairing_status,
            (unsigned)s_pair_trace.enc_status, (unsigned)s_pair_trace.disconnect_reason);
  return s_pair_trace_str;
}
#define PAIR_TRACE(stmt) stmt
#else
#define PAIR_TRACE(stmt)
#endif

static void prv_handle_connection_event(struct ble_gap_event *event) {
  // we only want to notify on a successful connection
  if (event->connect.status != 0)
    return;

  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(event->connect.conn_handle, &desc) != 0) {
    PBL_LOG_ERR("prv_handle_connection_event: Failed to find connection descriptor");
    return;
  }

  struct pbl_bt_conn_complete_event complete_event = {
    .handle = event->connect.conn_handle,
    .is_master = desc.role == BLE_GAP_ROLE_MASTER,
    .status = PBL_BT_HCI_STATUS_SUCCESS,
    .mtu = ble_att_mtu(event->connect.conn_handle),
  };

  // If OTA address != ID address, then the address must be resolved.
  // This happens for an already paired devices.
  complete_event.is_resolved = ble_addr_cmp(&desc.peer_id_addr, &desc.peer_ota_addr) != 0;

  {
    struct pbl_bt_addr ota_addr, id_addr;
    nimble_addr_to_pebble_addr(&desc.peer_ota_addr, &ota_addr);
    nimble_addr_to_pebble_addr(&desc.peer_id_addr, &id_addr);
    PBL_LOG_DBG("Conn compl: ota=" PBL_BT_ADDR_FMT " atype=%u", PBL_BT_ADDR_XPLODE(ota_addr),
                desc.peer_ota_addr.type);
    PBL_LOG_DBG("Conn compl: id=" PBL_BT_ADDR_FMT " atype=%u", PBL_BT_ADDR_XPLODE(id_addr),
                desc.peer_id_addr.type);
  }

  if (complete_event.is_resolved) {
    int rc;
    struct ble_store_key_sec key_sec;
    struct ble_store_value_sec value_sec;

    key_sec.idx = 0;
    key_sec.peer_addr = desc.peer_id_addr;

    rc = ble_store_read_peer_sec(&key_sec, &value_sec);
    if (rc != 0) {
      // We can get a resolved address in case of a repeated pairing event,
      // where peer security is deleted. An identity resolved event will be
      // received later after the new pairing is completed.
      PBL_LOG_INFO("Address resolved but no stored peer security (rc=%d)", rc);
      complete_event.is_resolved = false;
    } else {
      memcpy(complete_event.irk.data, value_sec.irk, 16);
    }
  } else {
    // If the address is not resolved, pairing is gonna happen.
    // Trigger name read to have it ready for the pairing confirmation.
    memset(s_device_name, 0, sizeof(s_device_name));
    uint16_t *conn_handle = kernel_malloc_check(sizeof(*conn_handle));
    *conn_handle = event->connect.conn_handle;
    nimble_gattc_op_queue_push(prv_device_name_read_op_start, conn_handle);
  }

  nimble_conn_params_to_pebble(&desc, &complete_event.conn_params);
  nimble_addr_to_pebble_device(&desc.peer_id_addr, &complete_event.peer_address);

  s_pairing_in_progress = false;

  pbl_bt_handle_le_connection_complete_event(&complete_event);
}

static void prv_end_pairing(uint16_t conn_handle, bool success) {
  if (!s_pairing_in_progress || conn_handle != s_pairing_conn_handle) {
    return;
  }

  struct pbl_bt_pairing_confirm_ctx *ctx =
      (struct pbl_bt_pairing_confirm_ctx *)((uintptr_t)conn_handle);
  pbl_bt_cb_pairing_confirm_handle_completed(ctx, success);
  s_pairing_in_progress = false;
}

static void prv_handle_disconnection_event(struct ble_gap_event *event) {
  PAIR_TRACE(s_pair_trace.disconnect_reason = event->disconnect.reason;)

  // A link dropped mid-pairing raises no pairing complete event.
  prv_end_pairing(event->disconnect.conn.conn_handle, false);

  struct pbl_bt_gatt_device_disconnection_event gatt_event;
  nimble_addr_to_pebble_addr(&event->disconnect.conn.peer_id_addr, &gatt_event.dev_address);
  pbl_bt_cb_gatt_handle_disconnect(&gatt_event);

  struct pbl_bt_disconn_complete_event disconnection_event = {
    .handle = event->disconnect.conn.conn_handle,
    .reason = event->disconnect.reason,
    .status = PBL_BT_HCI_STATUS_SUCCESS,
  };
  nimble_addr_to_pebble_device(&event->disconnect.conn.peer_id_addr,
                               &disconnection_event.peer_address);
  pbl_bt_handle_le_disconnection_complete_event(&disconnection_event);
}

#ifdef CONFIG_BT_OWN_SERVICE_CHANGED
// A phone that paired with the stock firmware keeps its attribute cache, even
// when it pairs again: Android drops it only on unbonding or on Service Changed.
// Phone apps keep the attributes they found at connect for the whole link,
// so drop the link a little after Service Changed: the reconnect uses ours.
#define SVC_CHANGED_RECONNECT_MS 1000
// After an update the phone rediscovers every handle while the watch discovers
// the phone's: give it time to finish, or a cut-short rediscovery leaves it
// with a mix of the old layout and ours.
#define SVC_CHANGED_UPDATE_RECONNECT_MS 10000

// While the link is kept responsive (an install, and 10 s after it), put the
// reconnect off so it does not cut a transfer short; up to 2 minutes.
#define SVC_CHANGED_DEFER_MS  2000
#define SVC_CHANGED_DEFER_MAX 60

static struct ble_npl_callout s_svc_changed_reconnect;
static uint16_t s_svc_changed_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_svc_changed_defers;

static bool prv_link_busy(uint16_t conn_handle) {
  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(conn_handle, &desc) != 0) {
    return false;
  }

  struct pbl_bt_addr addr;
  nimble_addr_to_pebble_addr(&desc.peer_id_addr, &addr);

  bool busy = false;
  bt_lock();
  GAPLEConnection *connection = gap_le_connection_by_addr(&addr);
  if (connection != nullptr) {
    busy = conn_mgr_get_latency_for_le_connection(connection, nullptr) ==
           PBL_BT_RESPONSE_TIME_MIN;
  }
  bt_unlock();

  return busy;
}

static void prv_svc_changed_reconnect_cb(struct ble_npl_event *ev) {
  if (s_svc_changed_defers < SVC_CHANGED_DEFER_MAX && prv_link_busy(s_svc_changed_conn)) {
    s_svc_changed_defers++;
    ble_npl_callout_reset(&s_svc_changed_reconnect,
                          ble_npl_time_ms_to_ticks32(SVC_CHANGED_DEFER_MS));
    return;
  }

  (void)ble_gap_terminate(s_svc_changed_conn, BLE_ERR_REM_USER_CONN_TERM);
  s_svc_changed_conn = BLE_HS_CONN_HANDLE_NONE;
}

static void prv_svc_changed_schedule_reconnect(uint16_t conn_handle, uint32_t delay_ms) {
  if (s_svc_changed_reconnect.handle == TIMER_INVALID_ID) {
    ble_npl_callout_init(&s_svc_changed_reconnect, nimble_port_get_dflt_eventq(),
                         prv_svc_changed_reconnect_cb, nullptr);
  }
  s_svc_changed_conn = conn_handle;
  s_svc_changed_defers = 0;
  ble_npl_callout_reset(&s_svc_changed_reconnect, ble_npl_time_ms_to_ticks32(delay_ms));
}

// An update from this firmware to one with the same attribute layout leaves the
// phone's cache right: no Service Changed, no reconnect. The layout is noted
// just before rebooting into the update and taken at the first encrypted link
// after the next boot, whatever its reason. Anything else (a factory reset, an
// install from the stock recovery, another layout) indicates as before.
#define SVC_LAYOUT_FILE      "bt_svc_layout"
#define SVC_LAYOUT_FILE_SIZE 128
static const uint8_t SVC_LAYOUT_KEY = 1;

// NimBLE's, declared only in its private headers.
void ble_gatts_lcl_svc_foreach(ble_gatt_svc_foreach_fn cb, void *arg);

static void prv_layout_hash_uuid(uint32_t *crc, const ble_uuid_t *uuid) {
  *crc = pbl_crc32(*crc, &uuid->type, sizeof(uuid->type));
  switch (uuid->type) {
    case BLE_UUID_TYPE_16:
      *crc = pbl_crc32(*crc, &BLE_UUID16(uuid)->value, sizeof(BLE_UUID16(uuid)->value));
      break;
    case BLE_UUID_TYPE_32:
      *crc = pbl_crc32(*crc, &BLE_UUID32(uuid)->value, sizeof(BLE_UUID32(uuid)->value));
      break;
    default:
      *crc = pbl_crc32(*crc, BLE_UUID128(uuid)->value, sizeof(BLE_UUID128(uuid)->value));
      break;
  }
}

static void prv_layout_hash_svc(const struct ble_gatt_svc_def *svc, uint16_t handle,
                                uint16_t end_group_handle, void *arg) {
  uint32_t *crc = arg;
  const uint16_t range[2] = {handle, end_group_handle};

  *crc = pbl_crc32(*crc, &svc->type, sizeof(svc->type));
  *crc = pbl_crc32(*crc, range, sizeof(range));
  prv_layout_hash_uuid(crc, svc->uuid);
  for (const struct ble_gatt_chr_def *chr = svc->characteristics; chr && chr->uuid; chr++) {
    prv_layout_hash_uuid(crc, chr->uuid);
    *crc = pbl_crc32(*crc, &chr->flags, sizeof(chr->flags));
    for (const struct ble_gatt_dsc_def *dsc = chr->descriptors; dsc && dsc->uuid; dsc++) {
      prv_layout_hash_uuid(crc, dsc->uuid);
      *crc = pbl_crc32(*crc, &dsc->att_flags, sizeof(dsc->att_flags));
    }
  }
}

static uint32_t prv_layout_hash(void) {
  uint32_t crc = 0;
  ble_gatts_lcl_svc_foreach(prv_layout_hash_svc, &crc);
  return crc;
}

void gatt_service_changed_note_update_layout(void) {
  const uint32_t hash = prv_layout_hash();
  SettingsFile file;
  if (settings_file_open(&file, SVC_LAYOUT_FILE, SVC_LAYOUT_FILE_SIZE) != S_SUCCESS) {
    return;
  }
  (void)settings_file_set(&file, &SVC_LAYOUT_KEY, sizeof(SVC_LAYOUT_KEY), &hash, sizeof(hash));
  settings_file_close(&file);
}

//! True if the phone may hold another attribute layout than ours. Computed once per boot.
static bool prv_layout_may_have_changed(void) {
  static bool s_checked;
  static bool s_changed;
  if (s_checked) {
    return s_changed;
  }
  s_checked = true;

  uint32_t noted = 0;
  bool have_noted = false;
  SettingsFile file;
  if (settings_file_open(&file, SVC_LAYOUT_FILE, SVC_LAYOUT_FILE_SIZE) == S_SUCCESS) {
    have_noted = settings_file_get(&file, &SVC_LAYOUT_KEY, sizeof(SVC_LAYOUT_KEY), &noted,
                                   sizeof(noted)) == S_SUCCESS;
    (void)settings_file_delete(&file, &SVC_LAYOUT_KEY, sizeof(SVC_LAYOUT_KEY));
    settings_file_close(&file);
  }

  s_changed = gatt_service_changed_server_fw_updated() &&
              !(have_noted && noted == prv_layout_hash());
#ifdef CONFIG_SERVICE_SHARED_PRF_STORAGE_IMPORT_STOCK
  // An imported stock bonding has the stock firmware's cache and no subscriptions.
  s_changed = s_changed || shared_prf_storage_imported_stock();
#endif
  PBL_LOG_INFO("Service Changed after update: %s", s_changed ? "needed" : "layout unchanged");
  return s_changed;
}

// After a firmware update the phone may hold the cache of another attribute
// layout, and it does not enable Service Changed again on a bonded link, so
// indicate on the first encrypted link. Once per phone and boot, so the
// rediscovery after the reconnect does not start another round.
static bool s_svc_changed_update_sent;
static ble_addr_t s_svc_changed_update_peer;

static void prv_svc_changed_after_update(uint16_t conn_handle,
                                         const struct ble_gap_conn_desc *desc) {
  if (!desc->sec_state.encrypted || !desc->sec_state.bonded || !prv_layout_may_have_changed() ||
      (s_svc_changed_update_sent && ble_addr_cmp(&desc->peer_id_addr,
                                                 &s_svc_changed_update_peer) == 0)) {
    return;
  }

  const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(PBL_BT_GATT_SERVICE_UUID);
  const ble_uuid16_t chr_uuid = BLE_UUID16_INIT(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16);
  uint16_t val_handle;
  if (ble_gatts_find_chr(&svc_uuid.u, &chr_uuid.u, nullptr, &val_handle) != 0) {
    return;
  }

  // Indicate without a stored subscription: every handle changed.
  const uint16_t range[2] = {htole16(0x0001), htole16(0xFFFF)};
  struct os_mbuf *om = ble_hs_mbuf_from_flat(range, sizeof(range));
  if (!om) {
    return;
  }
  int rc = ble_gatts_indicate_custom(conn_handle, val_handle, om);
  if (rc != 0) {
    PBL_LOG_ERR("Service Changed: indicate failed: 0x%04x", (uint16_t)rc);
    return;
  }
  PBL_LOG_INFO("Service Changed: indicated after update, reconnecting");
  s_svc_changed_update_sent = true;
  s_svc_changed_update_peer = desc->peer_id_addr;

  prv_svc_changed_schedule_reconnect(conn_handle, SVC_CHANGED_UPDATE_RECONNECT_MS);
}
#endif

static void prv_handle_enc_change_event(struct ble_gap_event *event) {
  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) != 0) {
    PBL_LOG_ERR("prv_handle_enc_change_event: Failed to find connection descriptor");
    return;
  }

  PAIR_TRACE(s_pair_trace.enc_status = event->enc_change.status;)
  PBL_LOG_INFO("Encryption change: status=0x%04x encrypted=%u bonded=%u",
               (uint16_t)event->enc_change.status, desc.sec_state.encrypted, desc.sec_state.bonded);

  struct pbl_bt_encryption_change enc_change_event = {
    .encryption_enabled = desc.sec_state.encrypted,
    .status = event->enc_change.status, // doesn't technically match but only logged so this is fine
  };
  nimble_addr_to_pebble_addr(&desc.peer_id_addr, &enc_change_event.dev_address);
  pbl_bt_handle_le_encryption_change_event(&enc_change_event);

#ifdef CONFIG_BT_OWN_SERVICE_CHANGED
  prv_svc_changed_after_update(event->enc_change.conn_handle, &desc);
#endif
}

static void prv_handle_conn_params_updated_event(struct ble_gap_event *event) {
  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) != 0) {
    PBL_LOG_ERR("prv_handle_conn_params_updated_event: Failed to find connection descriptor");
    return;
  }

  if (event->conn_update.status != 0) {
    PBL_LOG_ERR("Connection parameters update failed: 0x%04x", (uint16_t)event->conn_update.status);
  } else {
    PBL_LOG_INFO(
        "Connection parameters updated: "
        "itvl=%u ms, latency=%u, spvn timeout=%u ms",
        desc.conn_itvl * BLE_HCI_CONN_ITVL / 1000, desc.conn_latency,
        desc.supervision_timeout * BLE_HCI_CONN_SPVN_TMO_UNITS);
  }

  struct pbl_bt_conn_update_complete_event conn_params_update_event = {
    .status = nimble_hs_status_to_hci(event->conn_update.status),
  };
  nimble_conn_params_to_pebble(&desc, &conn_params_update_event.conn_params);
  nimble_addr_to_pebble_addr(&desc.peer_id_addr, &conn_params_update_event.dev_address);

  pbl_bt_handle_le_conn_params_update_event(&conn_params_update_event);
}

static void prv_handle_conn_update_req_event(struct ble_gap_event *event) {
  *event->conn_update_req.self_params = *event->conn_update_req.peer_params;

  PBL_LOG_INFO(
      "Connection update request: "
      "itvl=(%u, %u) ms, latency=%u, spvn timeout=%u ms",
      event->conn_update_req.self_params->itvl_min * BLE_HCI_CONN_ITVL / 1000,
      event->conn_update_req.self_params->itvl_max * BLE_HCI_CONN_ITVL / 1000,
      event->conn_update_req.self_params->latency,
      event->conn_update_req.self_params->supervision_timeout * BLE_HCI_CONN_SPVN_TMO_UNITS);
}

static void prv_handle_passkey_event(struct ble_gap_event *event) {
  char passkey_str[7];
  uint32_t passkey = 0;
  const char *device_name = nullptr;
  struct pbl_bt_pairing_confirm_ctx *ctx =
      (struct pbl_bt_pairing_confirm_ctx *)((uintptr_t)event->passkey.conn_handle);

  const uint16_t conn_handle = event->passkey.conn_handle;
  PAIR_TRACE(s_pair_trace.passkey_action = event->passkey.params.action;)

  switch (event->passkey.params.action) {
    case BLE_SM_IOACT_NUMCMP:
      passkey = event->passkey.params.numcmp;
      pairing_confirm_set_display_conn(BLE_HS_CONN_HANDLE_NONE);
      break;
    case BLE_SM_IOACT_DISP: {
      // Legacy passkey entry: show a random passkey for the phone's user to
      // type, and give it to NimBLE now.
      uint32_t random = 0U;
      if (!rng_rand(&random)) {
        PBL_LOG_WRN("No random passkey");
      }
      passkey = random % 1000000U;
      struct ble_sm_io io = {
        .action = BLE_SM_IOACT_DISP,
        .passkey = passkey,
      };
      const int rc = ble_sm_inject_io(conn_handle, &io);
      if (rc != 0) {
        PBL_LOG_ERR("ble_sm_inject_io (passkey) rc=0x%04x", (uint16_t)rc);
      }
      pairing_confirm_set_display_conn(conn_handle);
      break;
    }
    default:
      // No keyboard or out of band data to answer with.
      PBL_LOG_WRN("Unsupported pairing action %u", (unsigned)event->passkey.params.action);
      (void)ble_gap_terminate(conn_handle, BLE_ERR_AUTH_FAIL);
      return;
  }

  if (s_device_name[0] != '\0') {
    device_name = s_device_name;
  }

  PAIR_TRACE(s_pair_trace.passkeys++;)
  snprintf(passkey_str, sizeof(passkey_str), "%06" PRIu32, passkey);
  pbl_bt_cb_pairing_confirm_handle_request(ctx, device_name, passkey_str);
  s_pairing_in_progress = true;
  s_pairing_conn_handle = event->passkey.conn_handle;
}

static void prv_handle_pairing_complete_event(struct ble_gap_event *event) {
  PAIR_TRACE(s_pair_trace.pairing_status = event->pairing_complete.status;)
  PBL_LOG_INFO("Pairing complete: status=0x%04x", (uint16_t)event->pairing_complete.status);
  pairing_confirm_set_display_conn(BLE_HS_CONN_HANDLE_NONE);

  prv_end_pairing(event->pairing_complete.conn_handle, event->pairing_complete.status == 0);
}

static void prv_handle_identity_resolved_event(struct ble_gap_event *event) {
  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(event->identity_resolved.conn_handle, &desc) != 0) {
    PBL_LOG_ERR("prv_handle_identity_resolved_event: Failed to find connection descriptor");
    return;
  }

  struct pbl_bt_addr_change addr_change_event;
  nimble_addr_to_pebble_device(&desc.peer_ota_addr, &addr_change_event.device);
  nimble_addr_to_pebble_device(&desc.peer_id_addr, &addr_change_event.new_device);
  pbl_bt_handle_le_connection_handle_update_address(&addr_change_event);
}

static void prv_handle_mtu_change_event(struct ble_gap_event *event) {
  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(event->mtu.conn_handle, &desc) != 0) {
    PBL_LOG_ERR("prv_handle_mtu_change_event: Failed to find connection descriptor");
    return;
  }

  struct pbl_bt_gatt_device_mtu_update_event mtu_update_event = {.mtu = event->mtu.value};
  nimble_addr_to_pebble_addr(&desc.peer_id_addr, &mtu_update_event.dev_address);
  pbl_bt_cb_gatt_handle_mtu_update(&mtu_update_event);
}

extern int pebble_pairing_service_get_connectivity_send_notification(uint16_t conn_handle,
                                                                     uint16_t attr_handle);

#ifdef CONFIG_BT_OWN_SERVICE_CHANGED
// A phone enables Service Changed right after pairing. It may still hold the
// attribute cache of another firmware (the stock recovery it paired with),
// so tell it every handle changed and it rediscovers ours. Only on an
// encrypted link: iOS subscribes before pairing, and the reconnect below
// would cut its pairing short.
static void prv_svc_changed_after_pairing(const struct ble_gap_event *event) {
  struct ble_gap_conn_desc desc;
  if (event->subscribe.reason == BLE_GAP_SUBSCRIBE_REASON_WRITE &&
      event->subscribe.cur_indicate && !event->subscribe.prev_indicate &&
      ble_gap_conn_find(event->subscribe.conn_handle, &desc) == 0 &&
      desc.sec_state.encrypted) {
    const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(PBL_BT_GATT_SERVICE_UUID);
    const ble_uuid16_t chr_uuid = BLE_UUID16_INIT(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16);
    uint16_t val_handle;
    if (ble_gatts_find_chr(&svc_uuid.u, &chr_uuid.u, nullptr, &val_handle) == 0 &&
        event->subscribe.attr_handle == val_handle) {
      ble_svc_gatt_changed(0x0001, 0xFFFF);
      prv_svc_changed_schedule_reconnect(event->subscribe.conn_handle, SVC_CHANGED_RECONNECT_MS);
    }
  }
}
#endif

static void prv_handle_subscription_event(struct ble_gap_event *event) {
  PBL_LOG_DBG("prv_handle_subscription_event: connhandle: %d attr:%d notify:%d/%d indicate:%d/%d",
              event->subscribe.conn_handle, event->subscribe.attr_handle,
              event->subscribe.prev_notify, event->subscribe.cur_notify,
              event->subscribe.prev_indicate, event->subscribe.cur_indicate);
#ifdef CONFIG_BT_OWN_SERVICE_CHANGED
  // These watches indicate Service Changed themselves, with a reconnect, after pairing and after
  // an update. The Service Changed server would indicate again 10 s into every reconnect:
  // mid-link, the Core app rediscovers but loses its PPoGATT session, and the stalled link is
  // dropped and retried.
  prv_svc_changed_after_pairing(event);
#else
  const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(PBL_BT_GATT_SERVICE_UUID);
  const ble_uuid16_t chr_uuid = BLE_UUID16_INIT(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16);
  uint16_t val_handle;
  if (ble_gatts_find_chr(&svc_uuid.u, &chr_uuid.u, nullptr, &val_handle) != 0 ||
      event->subscribe.attr_handle != val_handle ||
      event->subscribe.cur_indicate == event->subscribe.prev_indicate) {
    return;
  }

  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(event->subscribe.conn_handle, &desc) != 0) {
    PBL_LOG_ERR("prv_handle_subscription_event: Failed to find connection descriptor");
    return;
  }

  struct pbl_bt_gatt_server_subscribe_event subscribe_event = {
    .connection_id = event->subscribe.conn_handle,
    .is_subscribing = event->subscribe.cur_indicate,
  };
  nimble_addr_to_pebble_addr(&desc.peer_id_addr, &subscribe_event.dev_address);
  pbl_bt_cb_gatt_service_changed_server_subscribe(&subscribe_event);
#endif
}

static void prv_handle_notification_rx_event(struct ble_gap_event *event) {
  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(event->notify_rx.conn_handle, &desc) != 0) {
    PBL_LOG_ERR("prv_handle_notification_rx_event: Failed to find connection descriptor");
    return;
  }

  struct pbl_bt_gatt_server_notif_indic_event notification_event = {
    .attr_handle = event->notify_rx.attr_handle,
    .attr_val = event->notify_rx.om->om_data,
    .attr_val_len = event->notify_rx.om->om_len,
  };
  nimble_addr_to_pebble_addr(&desc.peer_id_addr, &notification_event.dev_address);

  if (event->notify_rx.indication == 1) {
    pbl_bt_cb_gatt_handle_indication(&notification_event);
  } else {
    pbl_bt_cb_gatt_handle_notification(&notification_event);
  }
}

static void prv_handle_notification_tx_event(struct ble_gap_event *event) {
  PBL_LOG_DBG("notification tx event; status=%d attr_handle=%d indication=%d\n",
              event->notify_tx.status, event->notify_tx.attr_handle, event->notify_tx.indication);
}

static int prv_handle_pairing_request_event(struct ble_gap_event *event) {
  struct ble_gap_conn_desc desc;
  struct ble_store_value_sec value_sec;
  int rc;

  PAIR_TRACE(s_pair_trace.requests++;)

  if (pairability_is_enabled()) {
    return 0;
  }

  rc = ble_gap_conn_find(event->pairing_request.conn_handle, &desc);
  if (rc != 0) {
    return BLE_SM_ERR_UNSPECIFIED;
  }

  // Bonded peers are handled by the repeat pairing policy, where it lets them pair again.
  // Otherwise refuse now: an ignored request leaves the phone waiting out its 30 s SMP
  // timeout and retrying on every reconnect.
  struct ble_store_key_sec key_sec = {.peer_addr = desc.peer_id_addr};
  if (ble_store_read_peer_sec(&key_sec, &value_sec) == 0) {
#if defined(CONFIG_RECOVERY_FW) || \
    (MYNEWT_VAL(BLE_SM_SC_ONLY) && (MYNEWT_VAL(BLE_SM_IO_CAP) == BLE_HS_IO_DISPLAY_YESNO))
    return 0;
#else
    PBL_LOG_INFO("Pairing request from bonded peer rejected: not pairable");
    return BLE_SM_ERR_PAIR_NOT_SUPP;
#endif
  }

  PBL_LOG_INFO("Pairing request rejected: not pairable");
  return BLE_SM_ERR_PAIR_NOT_SUPP;
}

static int prv_handle_repeat_pairing_event(struct ble_gap_event *event) {
  // In recovery mode there is no UI that allows to manually delete a pairing,
  // so we unconditionally enable repeat pairing. In main firmware, allow it if
  // using secure connections and we support user confirmation; otherwise only
  // while the user has made the watch pairable (Bluetooth settings open), so a
  // phone whose keys no longer match (e.g. paired in the stock recovery) can
  // pair again without forgetting the watch on both sides.
#if !defined(CONFIG_RECOVERY_FW) && \
    !(MYNEWT_VAL(BLE_SM_SC_ONLY) && (MYNEWT_VAL(BLE_SM_IO_CAP) == BLE_HS_IO_DISPLAY_YESNO))
  if (!pairability_is_enabled()) {
    PBL_LOG_WRN("BLE_GAP_EVENT_REPEAT_PAIRING ignored");
    return BLE_GAP_REPEAT_PAIRING_IGNORE;
  }
#endif
  struct ble_gap_conn_desc desc;
  int ret;

  ret = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
  if (ret != 0) {
    return ret;
  }

  PBL_LOG_INFO("Repeat pairing: deleting stored peer keys and retrying");
  ble_store_util_delete_peer(&desc.peer_id_addr);

  return BLE_GAP_REPEAT_PAIRING_RETRY;
}

static void prv_handle_phy_update_event(struct ble_gap_event *event) {
  if (event->phy_updated.status != 0) {
    PBL_LOG_ERR("PHY update failed: 0x%04x", (uint16_t)event->phy_updated.status);
    return;
  }

  PBL_LOG_DBG("PHY update complete; conn_handle=%d, tx_phy=%d, rx_phy=%d",
              event->phy_updated.conn_handle, event->phy_updated.tx_phy, event->phy_updated.rx_phy);
}

static int prv_handle_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      PBL_LOG_DBG("BLE_GAP_EVENT_CONNECT");
      prv_handle_connection_event(event);
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      PBL_LOG_DBG("BLE_GAP_EVENT_DISCONNECT reason=0x%x", event->disconnect.reason);
      prv_handle_disconnection_event(event);
      break;
    case BLE_GAP_EVENT_ENC_CHANGE:
      PBL_LOG_DBG("BLE_GAP_EVENT_ENC_CHANGE");
      prv_handle_enc_change_event(event);
      break;
    case BLE_GAP_EVENT_CONN_UPDATE:
      PBL_LOG_DBG("BLE_GAP_EVENT_CONN_UPDATE");
      prv_handle_conn_params_updated_event(event);
      break;
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
      PBL_LOG_DBG("BLE_GAP_EVENT_CONN_UPDATE_REQ");
      prv_handle_conn_update_req_event(event);
      break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
      PBL_LOG_DBG("BLE_GAP_EVENT_PASSKEY_ACTION");
      prv_handle_passkey_event(event);
      break;
    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
      PBL_LOG_DBG("BLE_GAP_EVENT_IDENTITY_RESOLVED");
      prv_handle_identity_resolved_event(event);
      break;
    case BLE_GAP_EVENT_PAIRING_COMPLETE:
      PBL_LOG_DBG("BLE_GAP_EVENT_PAIRING_COMPLETE");
      prv_handle_pairing_complete_event(event);
      break;
    case BLE_GAP_EVENT_MTU:
      PBL_LOG_DBG("BLE_GAP_EVENT_MTU");
      prv_handle_mtu_change_event(event);
      break;
    case BLE_GAP_EVENT_SUBSCRIBE:
      PBL_LOG_DBG("BLE_GAP_EVENT_SUBSCRIBE");
      prv_handle_subscription_event(event);
      break;
    case BLE_GAP_EVENT_NOTIFY_RX:
      // no log here because it's incredibly noisy
      prv_handle_notification_rx_event(event);
      break;
    case BLE_GAP_EVENT_NOTIFY_TX:
      PBL_LOG_DBG("BLE_GAP_EVENT_NOTIFY_TX");
      prv_handle_notification_tx_event(event);
      break;
    case BLE_GAP_EVENT_PAIRING_REQUEST:
      PBL_LOG_DBG("BLE_GAP_EVENT_PAIRING_REQUEST");
      return prv_handle_pairing_request_event(event);
    case BLE_GAP_EVENT_REPEAT_PAIRING:
      PBL_LOG_DBG("BLE_GAP_EVENT_REPEAT_PAIRING");
      return prv_handle_repeat_pairing_event(event);
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
      PBL_LOG_DBG("BLE_GAP_EVENT_PHY_UPDATE_COMPLETE");
      prv_handle_phy_update_event(event);
      break;
    default:
      PBL_LOG_WRN("Unhandled GAP event: %d", event->type);
      break;
  }
  return 0;
}

bool pbl_bt_advert_advertising_enable(uint32_t min_interval_ms, uint32_t max_interval_ms) {
  int rc;
  uint8_t own_addr_type;
  struct ble_gap_adv_params advp = {
    .conn_mode = BLE_GAP_CONN_MODE_UND,
    .disc_mode = BLE_GAP_DISC_MODE_GEN,
    .itvl_min = BLE_GAP_ADV_ITVL_MS(min_interval_ms),
    .itvl_max = BLE_GAP_ADV_ITVL_MS(max_interval_ms),
  };

  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    PBL_LOG_ERR("Failed to infer own address type (%d)", rc);
    return false;
  }

  if (!ble_gap_adv_active()) {
    pbl_bt_id_sync_local_address();
  }

  rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &advp, prv_handle_gap_event,
                         nullptr);
  if (rc != 0) {
    PBL_LOG_ERR("Failed to start advertising (0x%04x)", (uint16_t)rc);
    return false;
  }

  return true;
}

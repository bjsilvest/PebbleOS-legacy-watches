/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pbl/bluetooth/hci_uart.h>
#include <pbl/drivers/uart.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/task_wdt/task_wdt.h>
#include <pbl/util/math.h>

#include <board/board.h>
#ifdef CONFIG_BT_CONTROLLER_DA1468X
#include <pbl/drivers/bluetooth/da1468x.h>
#include <pbl/drivers/rtc.h>
#include <pbl/soc/stm32/sleep.h>
#endif
#ifdef CONFIG_BT_CONTROLLER_CC2564
#include <pbl/drivers/bluetooth/cc2564.h>

#include <host/ble_store.h>
#include <mbedtls/aes.h>
#endif
#include <kernel/pebble_tasks.h>
#include <kernel/util/delay.h>
#include <system/passert.h>

// clang-format off
#include <os/os_mbuf.h>
// clang-format on
#include <nimble/hci_common.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_h4.h>
#include <nimble/transport_impl.h>
#include <os/endian.h>

PBL_LOG_MODULE_DECLARE(nimble, CONFIG_NIMBLE_LOG_LEVEL);

// Boards with split RX/TX UARTs define BT_HCI_UART_TX; BT_HCI_UART receives.
#ifndef BT_HCI_UART_TX
#define BT_HCI_UART_TX BT_HCI_UART
#endif

#define RX_RING_SIZE     1024U
#define EVT_POOL_POLL_MS 5U

static struct pbl_thread *s_hci_task_handle;
#if defined(CONFIG_BT_CONTROLLER_DA1468X) || defined(CONFIG_BT_CONTROLLER_CC2564)
// Booting the controller also runs on this stack.
PBL_THREAD_STACK_DEFINE(s_hci_task_stack, 2048);
#else
PBL_THREAD_STACK_DEFINE(s_hci_task_stack, 1024);
#endif
static PBL_SEM_DEFINE(s_rx_ready, 0, 1);
static PBL_SEM_DEFINE(s_acl_pool_avail, 0, 1);
static PBL_MUTEX_DEFINE(s_tx_mutex);
static struct hci_h4_sm s_hci_h4sm;

// Filled by the UART ISR, drained by the HCI task.
static uint8_t s_rx_ring[RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile bool s_rx_paused;

// Set once the controller can take HCI traffic (or never will).
static volatile enum {
  ControllerStatePending,
  ControllerStateReady,
  ControllerStateFailed,
} s_controller_state;

// The last command sent and the last command answered, for diagnostics.
static volatile uint16_t s_last_cmd;
static volatile uint16_t s_last_reply_cmd;
static volatile uint8_t s_last_reply_status;
static volatile uint32_t s_num_evts;
static volatile uint16_t s_last_fail_cmd;
static volatile uint8_t s_last_fail_status;
// ACL packets sent and completed as the controller reports them, and the
// length of the last command reply, to tell which check a host reset tripped.
static volatile uint32_t s_acl_tx;
#ifdef CONFIG_BT_CONTROLLER_DA1468X
// The UART has no flow control: count bytes lost to receive overruns.
static volatile uint32_t s_rx_overruns;
// Times the receive ring filled and CTS held the controller off.
static volatile uint32_t s_rx_pauses;
#endif
static volatile uint32_t s_acl_completed;
static volatile uint8_t s_last_reply_len;
static char s_reset_snapshot[32];

// SMP opcodes seen on the link, oldest first: '<' received, '>' sent, then the
// opcode in hex, and for Pairing Failed its reason.
#define SMP_TRACE_LEN    40U
#define L2CAP_CID_SMP    0x0006U
#define SMP_OP_PAIR_FAIL 0x05U

static char s_smp_trace[SMP_TRACE_LEN + 1];

static void prv_trace_append(const char *entry) {
  pbl_irq_lock();
  size_t len = strlen(s_smp_trace);
  const size_t add = strlen(entry);
  if (len + add > SMP_TRACE_LEN) {
    // Keep the newest entries.
    const size_t drop = len + add - SMP_TRACE_LEN;
    memmove(s_smp_trace, &s_smp_trace[drop], len - drop + 1U);
    len -= drop;
  }
  memcpy(&s_smp_trace[len], entry, add + 1U);
  pbl_irq_unlock();
}

// After an SMP PDU is sent, mark the next few completed packets with '+', to
// show whether the controller got it on air.
#define SMP_TRACE_COMPLETIONS 4U
static volatile uint8_t s_smp_completions_left;

static void prv_note_completed(uint16_t count) {
  while (count-- > 0U && s_smp_completions_left > 0U) {
    s_smp_completions_left--;
    prv_trace_append("+");
  }
}

static void prv_note_smp(char dir, struct os_mbuf *om) {
  // ACL header (handle + flags, length), L2CAP header (length, CID), opcode, reason
  uint8_t hdr[10];

  if (os_mbuf_copydata(om, 0, 9, hdr) != 0) {
    return;
  }
  // Only the first fragment of an L2CAP PDU carries its header.
  if (((hdr[1] >> 4) & 0x3U) == 0x1U) {
    return;
  }
  if ((hdr[6] | (hdr[7] << 8)) != L2CAP_CID_SMP) {
    return;
  }

  char entry[4] = {dir, "0123456789abcdef"[hdr[8] & 0xfU], '\0', '\0'};
  if (hdr[8] == SMP_OP_PAIR_FAIL && os_mbuf_copydata(om, 9, 1, &hdr[9]) == 0) {
    entry[2] = "0123456789abcdef"[hdr[9] & 0xfU];
  }
  prv_trace_append(entry);
  if (dir == '>') {
    s_smp_completions_left = SMP_TRACE_COMPLETIONS;
  }
}

const char *pbl_bt_hci_uart_smp_trace(void) {
  return s_smp_trace;
}

#ifdef CONFIG_BT_CONTROLLER_DA1468X
// The DA1468x controller reports more completed ACL packets than were sent
// (seen during pairing), which NimBLE takes as a broken controller and resets
// the host, dropping the link. Track what is outstanding per connection and
// clamp what the controller reports to it.
#define ACL_TRACK_CONNS 4U

static struct {
  bool used;
  uint16_t handle;
  uint16_t outstanding;
} s_acl_track[ACL_TRACK_CONNS];
static volatile uint32_t s_acl_clamped;

// Over the air an LE ACL PDU carries at most 27 bytes without data length
// extension. The controller completes larger HCI ACL packets once per LL
// fragment, which shows as over-reported completed packets, and pairing
// stalls right after the 69-byte SMP public key. Make the host fragment to
// the LL size instead.
#define LE_ACL_MAX_LEN 27U
static uint16_t s_le_acl_len_reported;

static void prv_clamp_le_buffer_size(struct ble_hci_ev *ev) {
  // Command Complete: num_cmds, opcode, status, LE ACL data packet length, ...
  if (ev->length < 6U || ev->data[3] != 0U) {
    return;
  }
  const uint16_t opcode = ev->data[1] | (ev->data[2] << 8);
  if (opcode != BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_BUF_SIZE) &&
      opcode != BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_BUF_SIZE_V2)) {
    return;
  }
  s_le_acl_len_reported = ev->data[4] | (ev->data[5] << 8);
  if (s_le_acl_len_reported > LE_ACL_MAX_LEN) {
    ev->data[4] = LE_ACL_MAX_LEN;
    ev->data[5] = 0U;
  }
}

static void prv_acl_track_sent(uint16_t handle) {
  pbl_irq_lock();
  size_t free_slot = ACL_TRACK_CONNS;
  for (size_t i = 0; i < ACL_TRACK_CONNS; i++) {
    if (s_acl_track[i].used && s_acl_track[i].handle == handle) {
      s_acl_track[i].outstanding++;
      pbl_irq_unlock();
      return;
    }
    if (!s_acl_track[i].used && free_slot == ACL_TRACK_CONNS) {
      free_slot = i;
    }
  }
  if (free_slot < ACL_TRACK_CONNS) {
    s_acl_track[free_slot].used = true;
    s_acl_track[free_slot].handle = handle;
    s_acl_track[free_slot].outstanding = 1U;
  }
  pbl_irq_unlock();
}

static uint16_t prv_acl_track_completed(uint16_t handle, uint16_t count) {
  pbl_irq_lock();
  for (size_t i = 0; i < ACL_TRACK_CONNS; i++) {
    if (s_acl_track[i].used && s_acl_track[i].handle == handle) {
      if (count > s_acl_track[i].outstanding) {
        count = s_acl_track[i].outstanding;
        s_acl_clamped++;
      }
      s_acl_track[i].outstanding -= count;
      pbl_irq_unlock();
      return count;
    }
  }
  pbl_irq_unlock();
  // Nothing was sent on this connection since it was tracked.
  if (count > 0U) {
    s_acl_clamped++;
  }
  return 0U;
}

static void prv_acl_track_forget(uint16_t handle) {
  pbl_irq_lock();
  for (size_t i = 0; i < ACL_TRACK_CONNS; i++) {
    if (s_acl_track[i].used && s_acl_track[i].handle == handle) {
      s_acl_track[i].used = false;
    }
  }
  pbl_irq_unlock();
}

static void prv_acl_track_reset(void) {
  pbl_irq_lock();
  memset(s_acl_track, 0, sizeof(s_acl_track));
  pbl_irq_unlock();
}
#endif

static struct os_mbuf *prv_alloc_acl_from_ll(void) {
  struct os_mbuf *om;

  // A NULL return is a fatal framing error for the H4 state machine, so wait
  // for the host to free a buffer. The UART backpressures meanwhile.
  while ((om = ble_transport_alloc_acl_from_ll()) == NULL) {
    (void)pbl_sem_take(&s_acl_pool_avail, PBL_MSEC(100));
  }

  return om;
}

static os_error_t prv_acl_put_signal(struct os_mempool_ext *mpe, void *data, void *arg) {
  os_error_t err = os_memblock_put_from_cb(&mpe->mpe_mp, data);
  pbl_sem_give(&s_acl_pool_avail);
  return err;
}

static void *prv_alloc_evt(int discardable) {
  void *buf;

  // The H4 state machine drops a discardable event without a buffer, but takes
  // NULL for any other as a fatal framing error: wait for the host to free one.
  while ((buf = ble_transport_alloc_evt(discardable)) == NULL && !discardable) {
    pbl_thread_sleep(PBL_MSEC(EVT_POOL_POLL_MS));
  }

  return buf;
}

static const struct hci_h4_allocators s_hci_h4_allocs_from_ll = {
  .acl = prv_alloc_acl_from_ll,
  .evt = prv_alloc_evt,
};

#ifdef CONFIG_BT_CONTROLLER_DA1468X
// Pinned resolvable private address wanted on the air in place of the identity address.
static uint8_t s_local_rpa[6];
static volatile bool s_local_rpa_set;
// Random address last sent, and the one the controller accepted (cleared by HCI Reset).
static uint8_t s_rnd_sent[6];
static uint8_t s_rnd_in_use[6];
static volatile bool s_rnd_in_use_valid;

#define HCI_OP_LE_SET_RANDOM_ADDR BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_RAND_ADDR)
#define HCI_OP_RESET              BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND, BLE_HCI_OCF_CB_RESET)
#define LE_CONN_COMPLETE_LEN      19U
#define LE_ENH_CONN_COMPLETE_LEN  31U
// Offset of the connection interval: the enhanced event inserts the local and peer RPAs here.
#define LE_CONN_COMPLETE_ITVL_OFF 12U

static bool prv_addr_is_rpa(const uint8_t *addr) {
  return (addr[5] & 0xc0U) == 0x40U;
}

//! When the controller uses a resolvable private address set as its random address, it reports
//! no local RPA. Report that address as our local RPA, so NimBLE pairs with the address on the
//! air while keeping the identity address. The legacy LE Connection Complete becomes the enhanced
//! one; the enhanced one, sent when the controller resolved the peer, gets the field filled in.
static void prv_conn_complete_add_local_rpa(struct ble_hci_ev *ev) {
  if (!s_rnd_in_use_valid || !prv_addr_is_rpa(s_rnd_in_use) ||
      ev->opcode != BLE_HCI_EVCODE_LE_META) {
    return;
  }
  uint8_t *rpas = &ev->data[LE_CONN_COMPLETE_ITVL_OFF];
  if (ev->length == LE_CONN_COMPLETE_LEN && ev->data[0] == BLE_HCI_LE_SUBEV_CONN_COMPLETE) {
    const size_t tail_len = LE_CONN_COMPLETE_LEN - LE_CONN_COMPLETE_ITVL_OFF;
    memmove(rpas + 12U, rpas, tail_len);
    memset(rpas + 6U, 0, 6U);
    ev->data[0] = BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE;
    ev->length = LE_ENH_CONN_COMPLETE_LEN;
  } else if (ev->length != LE_ENH_CONN_COMPLETE_LEN ||
             ev->data[0] != BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE ||
             memcmp(rpas, (const uint8_t[6]){0}, 6U) != 0) {
    return;
  }
  memcpy(rpas, s_rnd_in_use, 6U);
}

static void prv_note_rnd_addr_reply(struct ble_hci_ev *ev) {
  if (ev->opcode != BLE_HCI_EVCODE_COMMAND_COMPLETE || ev->length < 4U ||
      (ev->data[1] | (ev->data[2] << 8)) != HCI_OP_LE_SET_RANDOM_ADDR || ev->data[3] != 0U) {
    return;
  }
  memcpy(s_rnd_in_use, s_rnd_sent, sizeof(s_rnd_in_use));
  s_rnd_in_use_valid = true;
}
#endif

#ifdef CONFIG_BT_CONTROLLER_CC2564
// The CC2564B is a Bluetooth 4.1 controller: no resolving list (LL privacy)
// and no privacy modes. Answer those commands here instead, and resolve
// peers' resolvable private addresses against the bonded IRKs, reporting a
// resolved connection as the enhanced LE Connection Complete a 4.2
// controller would send. NimBLE then finds the bond as usual.
#define H4_SM_IDLE               0U // HCI_H4_SM_W4_PKT_TYPE in hci_h4.c
#define FAKE_REPLIES_MAX         4U
#define LE_CONN_COMPLETE_LEN     19U
#define LE_ENH_CONN_COMPLETE_LEN 31U
// Offset of the connection interval: the enhanced event inserts the local and peer RPAs here.
#define LE_CONN_COMPLETE_ITVL_OFF 12U

static uint16_t s_fake_replies[FAKE_REPLIES_MAX];
static volatile uint32_t s_fake_head;
static volatile uint32_t s_fake_tail;

static bool prv_is_ll_privacy_cmd(uint16_t opcode) {
  switch (opcode) {
    case BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ADD_RESOLV_LIST):
    case BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RMV_RESOLV_LIST):
    case BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CLR_RESOLV_LIST):
    case BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADDR_RES_EN):
    case BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_RPA_TMO):
    case BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_PRIVACY_MODE):
      return true;
    default:
      return false;
  }
}

//! Queue a successful Command Complete for @p opcode. The host waits for
//! each reply before sending the next command, so the queue stays short.
static bool prv_queue_fake_reply(uint16_t opcode) {
  bool queued = false;
  pbl_irq_lock();
  if (s_fake_head - s_fake_tail < FAKE_REPLIES_MAX) {
    s_fake_replies[s_fake_head % FAKE_REPLIES_MAX] = opcode;
    s_fake_head++;
    queued = true;
  }
  pbl_irq_unlock();
  pbl_sem_give(&s_rx_ready);
  return queued;
}

//! Only between packets, so a fake reply cannot split a real one.
static void prv_send_fake_replies(void) {
  while (s_fake_tail != s_fake_head && s_hci_h4sm.state == H4_SM_IDLE) {
    const uint16_t opcode = s_fake_replies[s_fake_tail % FAKE_REPLIES_MAX];
    s_fake_tail++;
    const uint8_t reply[] = {
      HCI_H4_EVT, BLE_HCI_EVCODE_COMMAND_COMPLETE, 4, 1, opcode & 0xffU, opcode >> 8U, 0,
    };
    hci_h4_sm_rx(&s_hci_h4sm, reply, sizeof(reply));
  }
}

static bool prv_addr_is_rpa(const uint8_t *addr) {
  return (addr[5] & 0xc0U) == 0x40U;
}

//! AES-128 on little-endian (HCI order) key, plain and cipher text, like LE Encrypt.
static bool prv_aes_le(const uint8_t *key, const uint8_t *plain, uint8_t *cipher) {
  uint8_t key_be[16];
  uint8_t plain_be[16];
  uint8_t cipher_be[16];
  for (int i = 0; i < 16; i++) {
    key_be[i] = key[15 - i];
    plain_be[i] = plain[15 - i];
  }

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  const bool ok = mbedtls_aes_setkey_enc(&ctx, key_be, 128) == 0 &&
                  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, plain_be, cipher_be) == 0;
  mbedtls_aes_free(&ctx);

  for (int i = 0; i < 16; i++) {
    cipher[i] = cipher_be[15 - i];
  }
  return ok;
}

//! ah(IRK, prand) == hash, with the address in HCI (little endian) order.
static bool prv_irk_resolves(const uint8_t *irk, const uint8_t *rpa) {
  uint8_t plain[16] = {0};
  uint8_t cipher[16];

  memcpy(plain, &rpa[3], 3);
  if (!prv_aes_le(irk, plain, cipher)) {
    return false;
  }
  return memcmp(cipher, rpa, 3) == 0;
}

struct ResolveContext {
  const uint8_t *rpa;
  ble_addr_t identity;
  bool found;
};

static int prv_resolve_cb(int obj_type, union ble_store_value *val, void *cookie) {
  struct ResolveContext *ctx = cookie;
  if (val->sec.irk_present && prv_irk_resolves(val->sec.irk, ctx->rpa)) {
    ctx->identity = val->sec.peer_addr;
    ctx->found = true;
    return 1;
  }
  return 0;
}

static void prv_conn_complete_resolve_peer(struct ble_hci_ev *ev) {
  if (ev->opcode != BLE_HCI_EVCODE_LE_META || ev->length != LE_CONN_COMPLETE_LEN ||
      ev->data[0] != BLE_HCI_LE_SUBEV_CONN_COMPLETE || ev->data[1] != 0U ||
      ev->data[5] != BLE_ADDR_RANDOM || !prv_addr_is_rpa(&ev->data[6])) {
    return;
  }

  uint8_t rpa[6];
  memcpy(rpa, &ev->data[6], sizeof(rpa));
  struct ResolveContext ctx = {.rpa = rpa};
  ble_store_iterate(BLE_STORE_OBJ_TYPE_PEER_SEC, prv_resolve_cb, &ctx);
  if (!ctx.found) {
    return;
  }

  uint8_t *rpas = &ev->data[LE_CONN_COMPLETE_ITVL_OFF];
  memmove(rpas + 12U, rpas, LE_CONN_COMPLETE_LEN - LE_CONN_COMPLETE_ITVL_OFF);
  memset(rpas, 0, 6U);
  memcpy(rpas + 6U, rpa, sizeof(rpa));
  memcpy(&ev->data[6], ctx.identity.val, sizeof(ctx.identity.val));
  ev->data[5] = (ctx.identity.type == BLE_ADDR_PUBLIC) ? BLE_ADDR_PUBLIC_ID : BLE_ADDR_RANDOM_ID;
  ev->data[0] = BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE;
  ev->length = LE_ENH_CONN_COMPLETE_LEN;
}
#endif

bool pbl_bt_hci_uart_local_addr_outdated(void) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  if (!s_rnd_in_use_valid) {
    return s_local_rpa_set;
  }
  if (s_local_rpa_set) {
    return memcmp(s_rnd_in_use, s_local_rpa, sizeof(s_local_rpa)) != 0;
  }
  return prv_addr_is_rpa(s_rnd_in_use);
#else
  return false;
#endif
}

void pbl_bt_hci_uart_set_local_rpa(const uint8_t *addr) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  pbl_mutex_lock(&s_tx_mutex, PBL_FOREVER);
  s_local_rpa_set = (addr != NULL);
  if (addr != NULL) {
    memcpy(s_local_rpa, addr, sizeof(s_local_rpa));
  }
  pbl_mutex_unlock(&s_tx_mutex);
#endif
}

static void prv_note_evt(struct ble_hci_ev *ev) {
  s_num_evts++;
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  prv_conn_complete_add_local_rpa(ev);
  prv_note_rnd_addr_reply(ev);
#elifdef CONFIG_BT_CONTROLLER_CC2564
  prv_conn_complete_resolve_peer(ev);
#endif
  if (ev->opcode == BLE_HCI_EVCODE_NUM_COMP_PKTS && ev->length >= 1U) {
    for (uint8_t i = 0; i < ev->data[0] && 1U + 4U * (i + 1U) <= ev->length; i++) {
      uint8_t *entry = &ev->data[1 + 4 * i];
      uint16_t count = entry[2] | (entry[3] << 8);
      s_acl_completed += count;
      prv_note_completed(count);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
      count = prv_acl_track_completed((entry[0] | (entry[1] << 8)) & 0x0fffU, count);
      entry[2] = count & 0xffU;
      entry[3] = count >> 8;
#endif
    }
    return;
  }
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  if (ev->opcode == BLE_HCI_EVCODE_DISCONN_CMP && ev->length >= 3U) {
    prv_acl_track_forget((ev->data[1] | (ev->data[2] << 8)) & 0x0fffU);
    prv_trace_append("x");
    s_smp_completions_left = 0U;
    return;
  }
#endif
  if (ev->opcode == BLE_HCI_EVCODE_COMMAND_COMPLETE ||
      ev->opcode == BLE_HCI_EVCODE_COMMAND_STATUS) {
    s_last_reply_len = ev->length;
  }
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  if (ev->opcode == BLE_HCI_EVCODE_COMMAND_COMPLETE) {
    prv_clamp_le_buffer_size(ev);
  }
#endif
  if (ev->opcode == BLE_HCI_EVCODE_COMMAND_COMPLETE && ev->length >= 4U) {
    s_last_reply_cmd = ev->data[1] | (ev->data[2] << 8);
    s_last_reply_status = ev->data[3];
  } else if (ev->opcode == BLE_HCI_EVCODE_COMMAND_STATUS && ev->length >= 4U) {
    s_last_reply_cmd = ev->data[2] | (ev->data[3] << 8);
    s_last_reply_status = ev->data[0];
  } else {
    return;
  }
  if (s_last_reply_status != 0U) {
    s_last_fail_cmd = s_last_reply_cmd;
    s_last_fail_status = s_last_reply_status;
  }
}

static int prv_hci_frame_cb(uint8_t pkt_type, void *data) {
  switch (pkt_type) {
    case HCI_H4_EVT:
      prv_note_evt(data);
      return ble_transport_to_hs_evt(data);
    case HCI_H4_ACL:
      prv_note_smp('<', data);
      return ble_transport_to_hs_acl(data);
    default:
      WTF;
  }

  return -1;
}

static void prv_rx_irq_handler(UARTDevice *dev, uint8_t data, const UARTRXErrorFlags *err_flags) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  if (err_flags->overrun_error) {
    s_rx_overruns++;
  }
#endif
  uint32_t head = s_rx_head;
  uint32_t next = (head + 1U) % RX_RING_SIZE;

  // The byte has already left the UART, so pause while there is still room
  // for it; the rest waits in the UART.
  s_rx_ring[head] = data;
  s_rx_head = next;

  if ((next + 1U) % RX_RING_SIZE == s_rx_tail) {
    s_rx_paused = true;
    uart_set_rx_interrupt_enabled(dev, false);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
    // Hold the controller off rather than lose what it sends meanwhile.
    da1468x_set_cts(false);
    s_rx_pauses++;
#endif
  }

  pbl_sem_give(&s_rx_ready);
}

#ifdef CONFIG_BT_CONTROLLER_DA1468X
// With flow control the controller's RTS gates each byte instead of pacing.
static bool s_flow_control;
static volatile uint32_t s_tx_flow_waits;
static volatile uint32_t s_tx_flow_forced;
static void prv_tx_irq_handler(UARTDevice *dev);
// 89 ms of data at 115200 baud.
static uint8_t s_rx_dma_buf[1024];
#endif

static void prv_uart_start(void) {
  // A controller driver that boots the controller has set the UART up already.
#if !defined(CONFIG_BT_CONTROLLER_DA1468X) && !defined(CONFIG_BT_CONTROLLER_CC2564)
  if (BT_HCI_UART_TX != BT_HCI_UART) {
    uart_init_rx_only(BT_HCI_UART);
    uart_init_tx_only(BT_HCI_UART_TX);
  } else {
    uart_init(BT_HCI_UART);
  }
#endif
  uart_set_rx_interrupt_handler(BT_HCI_UART, prv_rx_irq_handler);
  uart_set_rx_interrupt_enabled(BT_HCI_UART, true);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  uart_set_tx_interrupt_handler(BT_HCI_UART_TX, prv_tx_irq_handler);
  // Without flow control, a byte must be read before the next arrives (87 us),
  // which interrupts locked for longer (e.g. flash writes after pairing) broke:
  // receive by DMA where the board has it.
  uart_start_rx_dma(BT_HCI_UART, s_rx_dma_buf, sizeof(s_rx_dma_buf));
  s_flow_control = da1468x_has_flow_control();
#endif
  s_controller_state = ControllerStateReady;
}

#ifdef CONFIG_BT_CONTROLLER_DA1468X
// The DA1468x's DGTL transport also sends its own GTL messages (type 5, e.g.
// stack indications) and possibly other DGTL packet types on the HCI UART.
// NimBLE's H4 parser asserts on any packet type but HCI, so frame the stream
// here, pass HCI events and ACL data on whole, and skip the rest. A byte that
// starts no known packet is dropped to resynchronize.
#define DGTL_PKT_LOG     0x08U
#define DGTL_PKT_APP_RSP 0x07U
#define DGTL_PKT_GTL     0x05U
#define FRAME_MAX        (5U + 300U)

static struct {
  uint8_t buf[FRAME_MAX];
  uint16_t len;
  uint16_t hdr_len;
  uint32_t want; // total frame length once the header is in, else 0
  bool skip;     // a DGTL packet that is not HCI: count bytes but keep none
} s_frame;
static volatile uint32_t s_dgtl_skipped;
static volatile uint32_t s_rx_dropped_bytes;
static volatile uint16_t s_last_gtl_msg_id;
// GAPM_UNKNOWN_TASK_IND: the controller got a message no task handles, which
// leaves an HCI command unanswered. Keep which message and task it was.
#define GTL_GAPM_UNKNOWN_TASK_IND 0x0d1dU
static volatile uint16_t s_unknown_msg_id;
static volatile uint16_t s_unknown_task_id;

// LE Set Privacy Mode is Bluetooth 5.0; the DA14681 (4.2) rejects it, which
// makes NimBLE abandon adding a bonded peer to the resolving list. Without
// privacy modes a 4.2 controller behaves as in network mode, so answer it
// here with success instead.
#define HCI_OP_LE_SET_PRIVACY_MODE 0x204eU
static volatile bool s_fake_privacy_mode_reply;

static uint16_t prv_frame_hdr_len(uint8_t type) {
  switch (type) {
    case HCI_H4_ACL:
      return 5U;
    case HCI_H4_EVT:
      return 3U;
    case DGTL_PKT_GTL:
      return 9U;
    case DGTL_PKT_APP_RSP:
      return 4U;
    case DGTL_PKT_LOG:
      return 2U;
    default:
      return 0U;
  }
}

static uint32_t prv_frame_total_len(const uint8_t *hdr) {
  switch (hdr[0]) {
    case HCI_H4_ACL:
      return 5U + (hdr[3] | (hdr[4] << 8));
    case HCI_H4_EVT:
      return 3U + hdr[2];
    case DGTL_PKT_GTL:
      return 9U + (hdr[7] | (hdr[8] << 8));
    case DGTL_PKT_APP_RSP:
      return 4U + (hdr[2] | (hdr[3] << 8));
    case DGTL_PKT_LOG:
      return 2U + hdr[1];
    default:
      return 0U;
  }
}

static void prv_frame_byte(uint8_t c) {
  if (s_frame.len == 0U) {
    s_frame.hdr_len = prv_frame_hdr_len(c);
    if (s_frame.hdr_len == 0U) {
      s_rx_dropped_bytes++;
      return;
    }
    s_frame.want = 0U;
    s_frame.skip = false;
  }

  if (s_frame.len < sizeof(s_frame.buf)) {
    s_frame.buf[s_frame.len] = c;
  }
  s_frame.len++;

  if (s_frame.want == 0U) {
    if (s_frame.len < s_frame.hdr_len) {
      return;
    }
    s_frame.want = prv_frame_total_len(s_frame.buf);
    const uint8_t type = s_frame.buf[0];
    s_frame.skip = (type != HCI_H4_ACL && type != HCI_H4_EVT) || s_frame.want > FRAME_MAX;
    if (type == DGTL_PKT_GTL) {
      s_last_gtl_msg_id = s_frame.buf[1] | (s_frame.buf[2] << 8);
    }
  }

  if (s_frame.len < s_frame.want) {
    return;
  }

  if (s_frame.skip) {
    s_dgtl_skipped++;
    if (s_frame.buf[0] == DGTL_PKT_GTL && s_last_gtl_msg_id == GTL_GAPM_UNKNOWN_TASK_IND &&
        s_frame.len >= 13U && s_frame.len <= sizeof(s_frame.buf)) {
      s_unknown_msg_id = s_frame.buf[9] | (s_frame.buf[10] << 8);
      s_unknown_task_id = s_frame.buf[11] | (s_frame.buf[12] << 8);
    }
  } else {
    int consumed = hci_h4_sm_rx(&s_hci_h4sm, s_frame.buf, s_frame.len);
    if (consumed != (int)s_frame.len) {
      PBL_LOG_ERR("hci_h4_sm_rx returned %d of %u", consumed, (unsigned)s_frame.len);
    }
  }
  s_frame.len = 0U;
}
#endif

#ifdef CONFIG_BT_CONTROLLER_DA1468X
// STOP mode stops the UART clocks. To allow it while the controller runs, the
// host deasserts CTS after a short idle time, so the controller holds what it
// has, and the controller raises its wake line while it has data, which
// wakes the host to reassert CTS. Awake holds one STOP block (taken by
// da1468x_boot()).
#define BT_IDLE_MS 20U

static bool s_power_save;
static volatile bool s_awake = true;
static volatile RtcTicks s_last_activity;
static volatile uint32_t s_tx_busy;
static volatile uint32_t s_sleeps;

// With interrupts locked.
static void prv_wake_locked(void) {
  s_last_activity = rtc_get_ticks();
  if (!s_awake) {
    s_awake = true;
    soc_stm32_stop_block();
    da1468x_set_cts(true);
  }
}

static void prv_wake(void) {
  pbl_irq_lock();
  prv_wake_locked();
  pbl_irq_unlock();
}

static void prv_wake_irq_handler(void) {
  prv_wake();
  // The HCI thread waits for an idle timeout again while awake.
  pbl_sem_give(&s_rx_ready);
}

static bool prv_idle_locked(void) {
  return s_awake && s_tx_busy == 0U && !s_rx_paused && s_rx_tail == s_rx_head &&
         s_frame.len == 0U && !da1468x_wake_pending() &&
         (rtc_get_ticks() - s_last_activity) >= (BT_IDLE_MS * RTC_TICKS_HZ) / 1000U;
}

static void prv_try_sleep(void) {
  if (!s_power_save) {
    return;
  }

  pbl_irq_lock();
  if (!prv_idle_locked()) {
    pbl_irq_unlock();
    return;
  }
  da1468x_set_cts(false);
  pbl_irq_unlock();

  // Let a byte already on the wire land and the UART idle interrupt hand it on.
  delay_us(50);

  pbl_irq_lock();
  if (prv_idle_locked()) {
    s_awake = false;
    s_sleeps++;
    soc_stm32_stop_release();
  } else {
    da1468x_set_cts(true);
  }
  pbl_irq_unlock();
}
#endif

static void prv_hci_task_main(void *unused) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  // Loading the controller takes about a second, so it happens here rather
  // than in ble_transport_ll_init(). A failure leaves Bluetooth off instead of
  // crashing, since a crash loop would end in the bootloader starting PRF.
  if (da1468x_boot()) {
    prv_uart_start();
    s_last_activity = rtc_get_ticks();
    s_power_save = s_flow_control && da1468x_has_wake();
    if (s_power_save) {
      da1468x_wake_irq_init(prv_wake_irq_handler);
    }
  } else {
    s_controller_state = ControllerStateFailed;
  }

  // Waiting for events from the host to free buffers can deadlock against it:
  // turn a stuck thread into a reset that records where it was.
  PBL_ASSERTN(pbl_task_wdt_add(nullptr, CONFIG_TASK_WDT_TIMEOUT_MS, nullptr, nullptr) >= 0);
#elifdef CONFIG_BT_CONTROLLER_CC2564
  // Loading TI's init script takes a second or two, so it happens here rather
  // than in ble_transport_ll_init(). A failure leaves Bluetooth off instead of
  // crashing, since a crash loop would end in the bootloader starting PRF.
  if (cc2564_boot()) {
    prv_uart_start();
  } else {
    s_controller_state = ControllerStateFailed;
  }
#endif

  while (true) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
    pbl_task_wdt_set_waiting(true);
    // While awake, wake up to check for idle.
    const bool timed_out =
        pbl_sem_take(&s_rx_ready, (s_power_save && s_awake) ? PBL_MSEC(BT_IDLE_MS) : PBL_FOREVER) !=
        0;
    pbl_task_wdt_set_waiting(false);
    if (s_rx_tail != s_rx_head) {
      prv_wake();
    }
#else
    pbl_sem_take(&s_rx_ready, PBL_FOREVER);
#endif

    while (s_rx_tail != s_rx_head) {
      uint32_t tail = s_rx_tail;
      uint32_t head = s_rx_head;
      uint32_t len = (head > tail) ? (head - tail) : (RX_RING_SIZE - tail);

#ifdef CONFIG_BT_CONTROLLER_DA1468X
      for (uint32_t i = 0; i < len; i++) {
        prv_frame_byte(s_rx_ring[tail + i]);
      }
      int consumed = len;
#else
      int consumed = hci_h4_sm_rx(&s_hci_h4sm, &s_rx_ring[tail], len);
      if (consumed <= 0) {
        PBL_LOG_ERR("hci_h4_sm_rx returned %d", consumed);
        consumed = len;
      }
#endif
      s_rx_tail = (tail + consumed) % RX_RING_SIZE;

      if (s_rx_paused) {
        s_rx_paused = false;
        uart_set_rx_interrupt_enabled(BT_HCI_UART, true);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
        da1468x_set_cts(true);
#endif
      }
    }

#ifdef CONFIG_BT_CONTROLLER_CC2564
    prv_send_fake_replies();
#endif

#ifdef CONFIG_BT_CONTROLLER_DA1468X
    // Only between packets, so the fake reply cannot split a real one.
    if (s_fake_privacy_mode_reply && s_frame.len == 0U) {
      static const uint8_t reply[] = {
        HCI_H4_EVT, BLE_HCI_EVCODE_COMMAND_COMPLETE,    4,
        1,          HCI_OP_LE_SET_PRIVACY_MODE & 0xffU, HCI_OP_LE_SET_PRIVACY_MODE >> 8,
        0
      };
      s_fake_privacy_mode_reply = false;
      for (size_t i = 0; i < sizeof(reply); i++) {
        prv_frame_byte(reply[i]);
      }
    }

    if (timed_out || !s_awake) {
      prv_try_sleep();
    }
#endif
  }
}

#ifdef CONFIG_BT_CONTROLLER_DA1468X
// The DA1468x's DGTL reads each packet in steps (type, header, parameters),
// re-arming the read in between, and its UART has only a 16-byte FIFO. Bytes
// lost there make the DGTL drop everything until the next HCI Reset. When the
// controller image drives RTS (BOARD_BT_HAS_FLOW_CONTROL), each byte waits for
// it. Otherwise send in short bursts with a pause after each, and after each
// packet type byte, which still loses bytes when the controller is busy.
//
// The host task runs at nearly the highest priority, so bytes go out from the
// TX interrupt and waits sleep: busy-waiting starved the UI.
#define TX_BURST_BYTES      8U
#define TX_PAUSE_MS         1U
#define TX_BURST_TIMEOUT_MS 50U

static const uint8_t *s_tx_ptr;
static volatile size_t s_tx_left;
static PBL_SEM_DEFINE(s_tx_done, 0, 1);
// RTS stuck deasserted this long means the line is not what we think: send.
#define TX_FLOW_TIMEOUT_MS 500U
#define TX_FLOW_SPIN_TRIES 10
#define TX_FLOW_SPIN_US    10U

static void prv_tx_irq_handler(UARTDevice *dev) {
  if (s_tx_left == 0U) {
    uart_set_tx_interrupt_enabled(dev, false);
    return;
  }
  if (s_flow_control && !da1468x_rts_ready()) {
    // No EXTI line is free for RTS on silk: stop and let the writer poll.
    uart_set_tx_interrupt_enabled(dev, false);
    pbl_sem_give(&s_tx_done);
    return;
  }
  uart_write_byte(dev, *s_tx_ptr++);
  if (--s_tx_left == 0U) {
    uart_set_tx_interrupt_enabled(dev, false);
    pbl_sem_give(&s_tx_done);
  }
}

static void prv_tx_poll_rest(void) {
  pbl_irq_lock();
  uart_set_tx_interrupt_enabled(BT_HCI_UART_TX, false);
  const uint8_t *ptr = s_tx_ptr;
  size_t left = s_tx_left;
  s_tx_left = 0U;
  pbl_irq_unlock();
  while (left-- > 0U) {
    uart_write_byte(BT_HCI_UART_TX, *ptr++);
  }
  (void)pbl_sem_take(&s_tx_done, PBL_NO_WAIT);
}

// Send from the TX interrupt; the caller sleeps meanwhile.
static void prv_write_irq(const uint8_t *data, size_t len) {
  uint32_t waited_ms = 0U;

  s_tx_ptr = data;
  s_tx_left = len;
  while (s_tx_left > 0U) {
    uart_set_tx_interrupt_enabled(BT_HCI_UART_TX, true);
    if (pbl_sem_take(&s_tx_done, PBL_MSEC(TX_BURST_TIMEOUT_MS)) != 0) {
      // Should not happen; finish by polling rather than lose the bytes.
      prv_tx_poll_rest();
      return;
    }
    if (s_tx_left > 0U) {
      // The controller deasserted RTS. Usually only until its DMA takes the
      // byte in its FIFO, so look again briefly before sleeping.
      s_tx_flow_waits++;
      for (int i = 0; i < TX_FLOW_SPIN_TRIES && !da1468x_rts_ready(); i++) {
        delay_us(TX_FLOW_SPIN_US);
      }
      if (da1468x_rts_ready()) {
        continue;
      }
      if (waited_ms >= TX_FLOW_TIMEOUT_MS) {
        s_tx_flow_forced++;
        prv_tx_poll_rest();
        return;
      }
      pbl_thread_sleep(PBL_MSEC(1));
      waited_ms++;
    }
  }
}
#endif

static void prv_write(const uint8_t *data, size_t len) {
  if (s_controller_state != ControllerStateReady) {
    return;
  }

#ifdef CONFIG_BT_CONTROLLER_DA1468X
  // Stay awake for the reply.
  pbl_irq_lock();
  s_tx_busy++;
  prv_wake_locked();
  pbl_irq_unlock();
  if (s_flow_control) {
    prv_write_irq(data, len);
  } else {
    for (size_t i = 0U; i < len; i += TX_BURST_BYTES) {
      prv_write_irq(&data[i], MIN(len - i, (size_t)TX_BURST_BYTES));
      pbl_thread_sleep(PBL_MSEC(TX_PAUSE_MS));
    }
  }
  pbl_irq_lock();
  s_tx_busy--;
  s_last_activity = rtc_get_ticks();
  pbl_irq_unlock();
#else
  for (size_t i = 0U; i < len; i++) {
    uart_write_byte(BT_HCI_UART_TX, data[i]);
  }
#endif
}

static void prv_write_mbuf(uint8_t type, struct os_mbuf *om) {
  if (type == HCI_H4_ACL) {
    s_acl_tx++;
    prv_note_smp('>', om);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
    if (om->om_len >= 2U) {
      prv_acl_track_sent((om->om_data[0] | (om->om_data[1] << 8)) & 0x0fffU);
    }
#endif
  }
  pbl_mutex_lock(&s_tx_mutex, PBL_FOREVER);
  prv_write(&type, 1U);
  for (struct os_mbuf *x = om; x != NULL; x = SLIST_NEXT(x, om_next)) {
    prv_write(x->om_data, x->om_len);
  }
  pbl_mutex_unlock(&s_tx_mutex);

  os_mbuf_free_chain(om);
}

void ble_transport_ll_init(void) {
  hci_h4_sm_init(&s_hci_h4sm, &s_hci_h4_allocs_from_ll, prv_hci_frame_cb);
  ble_transport_register_put_acl_from_ll_cb(prv_acl_put_signal);

  struct pbl_thread_attr attr = {
    .name = "NimbleHCI",
    .entry = prv_hci_task_main,
    .prio = PBL_PRIO_IDLE + 3,
    .privileged = true,
    .stack = s_hci_task_stack,
    .stack_size = sizeof(s_hci_task_stack),
  };

#if !defined(CONFIG_BT_CONTROLLER_DA1468X) && !defined(CONFIG_BT_CONTROLLER_CC2564)
  prv_uart_start();
#endif

  s_hci_task_handle = pebble_task_create(PebbleTask_BTHCI, &attr);
  PBL_ASSERTN(s_hci_task_handle);
}

bool pbl_bt_hci_uart_wait_ready(uint32_t timeout_ms) {
  const uint32_t step_ms = 50U;

  for (uint32_t waited = 0U; s_controller_state == ControllerStatePending; waited += step_ms) {
    if (waited >= timeout_ms) {
      return false;
    }
    pbl_task_wdt_feed_self();
    pbl_thread_sleep(PBL_MSEC(step_ms));
  }

  return s_controller_state == ControllerStateReady;
}

void pbl_bt_hci_uart_snapshot(void) {
  // Keep the first reset since boot: later ones follow from it.
  if (s_reset_snapshot[0] != '\0') {
    return;
  }
  sniprintf(s_reset_snapshot, sizeof(s_reset_snapshot), "%04x>%04x/%u a%lu n%lu",
            (unsigned)s_last_cmd, (unsigned)s_last_reply_cmd, (unsigned)s_last_reply_len,
            (unsigned long)s_acl_tx, (unsigned long)s_acl_completed);
}

uint32_t pbl_bt_hci_uart_acl_clamped(void) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  return s_acl_clamped;
#else
  return 0U;
#endif
}

void pbl_bt_hci_uart_copy_rx_stats(char *buf, size_t len) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  sniprintf(buf, len, "%c%lu!%lu z%lu r%lu o%lu d%lu u%04x@%04x g%lu/%04x",
            s_flow_control ? 'F' : 'p', (unsigned long)s_tx_flow_waits,
            (unsigned long)s_tx_flow_forced, (unsigned long)s_sleeps, (unsigned long)s_rx_pauses,
            (unsigned long)s_rx_overruns, (unsigned long)s_rx_dropped_bytes,
            (unsigned)s_unknown_msg_id, (unsigned)s_unknown_task_id, (unsigned long)s_dgtl_skipped,
            (unsigned)s_last_gtl_msg_id);
#else
  buf[0] = '\0';
#endif
}

uint16_t pbl_bt_hci_uart_le_acl_len_reported(void) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  return s_le_acl_len_reported;
#else
  return 0U;
#endif
}

const char *pbl_bt_hci_uart_reset_snapshot(void) {
  return s_reset_snapshot;
}

void pbl_bt_hci_uart_copy_status(char *buf, size_t len) {
  sniprintf(buf, len, "%04x:%02x !%04x:%02x", (unsigned)s_last_reply_cmd,
            (unsigned)s_last_reply_status, (unsigned)s_last_fail_cmd, (unsigned)s_last_fail_status);
}

int ble_transport_to_ll_cmd_impl(void *buf) {
  struct ble_hci_cmd *cmd = buf;
  uint8_t type = HCI_H4_CMD;

  s_last_cmd = le16toh(cmd->opcode);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  if (s_last_cmd == BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND, BLE_HCI_OCF_CB_RESET)) {
    prv_acl_track_reset();
  }
  if (s_last_cmd == HCI_OP_LE_SET_PRIVACY_MODE) {
    ble_transport_free(buf);
    s_fake_privacy_mode_reply = true;
    pbl_sem_give(&s_rx_ready);
    return 0;
  }
#endif

#ifdef CONFIG_BT_CONTROLLER_CC2564
  if (prv_is_ll_privacy_cmd(s_last_cmd) && prv_queue_fake_reply(s_last_cmd)) {
    ble_transport_free(buf);
    return 0;
  }
#endif

  pbl_mutex_lock(&s_tx_mutex, PBL_FOREVER);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  if (s_last_cmd == HCI_OP_LE_SET_RANDOM_ADDR && cmd->length == sizeof(s_local_rpa)) {
    if (s_local_rpa_set) {
      memcpy(cmd->data, s_local_rpa, sizeof(s_local_rpa));
    }
    memcpy(s_rnd_sent, cmd->data, sizeof(s_rnd_sent));
  } else if (s_last_cmd == HCI_OP_RESET) {
    s_rnd_in_use_valid = false;
  }
#endif
  prv_write(&type, 1U);
  prv_write(buf, sizeof(*cmd) + cmd->length);
  pbl_mutex_unlock(&s_tx_mutex);

  ble_transport_free(buf);

  return 0;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *om) {
  prv_write_mbuf(HCI_H4_ACL, om);
  return 0;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *om) {
  prv_write_mbuf(HCI_H4_ISO, om);
  return 0;
}

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

#include <board/board.h>
#ifdef CONFIG_BT_CONTROLLER_DA1468X
#include <pbl/drivers/bluetooth/da1468x.h>
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
#ifdef CONFIG_BT_CONTROLLER_DA1468X
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

static void prv_note_evt(struct ble_hci_ev *ev) {
  s_num_evts++;
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
  }

  pbl_sem_give(&s_rx_ready);
}

static void prv_uart_start(void) {
#ifndef CONFIG_BT_CONTROLLER_DA1468X
  if (BT_HCI_UART_TX != BT_HCI_UART) {
    uart_init_rx_only(BT_HCI_UART);
    uart_init_tx_only(BT_HCI_UART_TX);
  } else {
    uart_init(BT_HCI_UART);
  }
#endif
  uart_set_rx_interrupt_handler(BT_HCI_UART, prv_rx_irq_handler);
  uart_set_rx_interrupt_enabled(BT_HCI_UART, true);
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
  } else {
    int consumed = hci_h4_sm_rx(&s_hci_h4sm, s_frame.buf, s_frame.len);
    if (consumed != (int)s_frame.len) {
      PBL_LOG_ERR("hci_h4_sm_rx returned %d of %u", consumed, (unsigned)s_frame.len);
    }
  }
  s_frame.len = 0U;
}
#endif

static void prv_hci_task_main(void *unused) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
  // Loading the controller takes about a second, so it happens here rather
  // than in ble_transport_ll_init(). A failure leaves Bluetooth off instead of
  // crashing, since a crash loop would end in the bootloader starting PRF.
  if (da1468x_boot()) {
    prv_uart_start();
  } else {
    s_controller_state = ControllerStateFailed;
  }

  // Waiting for events from the host to free buffers can deadlock against it:
  // turn a stuck thread into a reset that records where it was.
  PBL_ASSERTN(pbl_task_wdt_add(nullptr, CONFIG_TASK_WDT_TIMEOUT_MS, nullptr, nullptr) >= 0);
#endif

  while (true) {
#ifdef CONFIG_BT_CONTROLLER_DA1468X
    pbl_task_wdt_set_waiting(true);
#endif
    pbl_sem_take(&s_rx_ready, PBL_FOREVER);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
    pbl_task_wdt_set_waiting(false);
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
      }
    }
  }
}

#ifdef CONFIG_BT_CONTROLLER_DA1468X
// The DA1468x's DGTL reads each packet in steps (type, header, parameters),
// re-arming the read in between, and its UART has only a 16-byte FIFO and no
// flow control here. Long bursts lost bytes: pairing failed on the 69-byte
// SMP public key, which the phone rejected as invalid. Send in short bursts
// with a pause after each, and after each packet type byte.
#define TX_BURST_BYTES 8U
#define TX_PAUSE_US    300U

static void prv_tx_pause(void) {
  uart_wait_for_tx_complete(BT_HCI_UART_TX);
  delay_us(TX_PAUSE_US);
}
#endif

static void prv_write(const uint8_t *data, size_t len) {
  if (s_controller_state != ControllerStateReady) {
    return;
  }

  for (size_t i = 0U; i < len; i++) {
    uart_write_byte(BT_HCI_UART_TX, data[i]);
#ifdef CONFIG_BT_CONTROLLER_DA1468X
    if (len == 1U || (i + 1U) % TX_BURST_BYTES == 0U) {
      prv_tx_pause();
    }
#endif
  }
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

#ifndef CONFIG_BT_CONTROLLER_DA1468X
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
  sniprintf(buf, len, "g%lu/%04x d%lu o%lu", (unsigned long)s_dgtl_skipped,
            (unsigned)s_last_gtl_msg_id, (unsigned long)s_rx_dropped_bytes,
            (unsigned long)s_rx_overruns);
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
#endif

  pbl_mutex_lock(&s_tx_mutex, PBL_FOREVER);
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

/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Boots a DA1468x Bluetooth controller from RAM over its bootrom UART.
//!
//! The bootrom protocol is the one stock silk used: after reset the bootrom
//! prints its chip ID (AE silicon and later) and sends STX; the host answers
//! SOH and a 16-bit length, gets ACK, sends the image, and confirms the
//! returned XOR checksum with ACK. That loads stage 2, which takes the
//! controller image at 1 Mbaud:
//!
//!   host -> "PBL2" len:u32 crc32:u32   stage 2 -> 'K'
//!   host -> len bytes                  stage 2 -> 'O' (runs it) or 'C'

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pbl/crc/crc.h>
#include <pbl/drivers/bluetooth/da1468x.h>
#include <pbl/drivers/exti.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/rtc.h>
#include <pbl/drivers/uart.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/stm32/sleep.h>
#include <pbl/util/math.h>

#include <board/board.h>
#include <kernel/util/delay.h>

PBL_LOG_MODULE_DEFINE(driver_bt_da1468x, CONFIG_BT_LOG_LEVEL);

extern const uint8_t da1468x_stage2_start[], da1468x_stage2_end[];
extern const uint8_t da1468x_controller_start[], da1468x_controller_end[];

#define BOOTROM_BAUD 57600U
#define STAGE2_BAUD  1000000U

// AE silicon takes 220-400 ms from reset to its first chip ID character.
#define BOOTROM_FIRST_CHAR_MS 600U
#define BOOTROM_CHAR_MS       100U
#define BOOTROM_MAX_ID_LEN    48U
#define STAGE2_START_MS       5U
#define STAGE2_REPLY_MS       100U
// Stage 2 checksums the image bytewise at 16 MHz, about 0.3 s.
#define STAGE2_CRC_MS 1500U
#define BOOT_ATTEMPTS 3
// The controller image needs a moment to start its UART after stage 2 jumps.
#define HCI_PROBE_ATTEMPTS 10
#define HCI_PROBE_REPLY_MS 200U
// The controller image sends one marker byte per init step before HCI is up
// ("m", "c", "p", "b"), the last one just before its HCI transport starts,
// then a vendor event (04 ff 01 'H') through the HCI transport.
#define MARKERS_MS       1000U
#define MARKER_LAST      'b'
#define MARKERS_HELLO_MS 300U
#define MAX_MARKERS      8U
// What the controller sends after its markers and in reply to HCI Reset is
// kept as hex for the status, up to this many bytes.
#define RX_HEX_BYTES 8U
#define MAX_RX_AFTER 256U
#define MAX_PROBE_RX 64U

#define SOH 0x01U
#define STX 0x02U
#define ACK 0x06U

// Replies are read from the RX interrupt, so waiting for one does not starve
// lower priority threads (and with them the task watchdog).
#define RX_RING_SIZE 64U

static uint8_t s_rx_ring[RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static PBL_SEM_DEFINE(s_rx_ready, 0, 1);

static char s_status[32] = "Not started";
static char s_chip_id[24];
static char s_markers[MAX_MARKERS + 1];
static volatile uint32_t s_rx_framing_errors;
static uint32_t s_rx_after_count;
static char s_rx_after_hex[2 * RX_HEX_BYTES + 1];
static char s_rx_probe_hex[2 * RX_HEX_BYTES + 1];

static void prv_rx_irq_handler(UARTDevice *dev, uint8_t data, const UARTRXErrorFlags *err_flags) {
  // The bootrom drives the lines while probing other interfaces, which can
  // show up as framing errors: drop those bytes.
  if (err_flags->framing_error) {
    s_rx_framing_errors++;
    return;
  }

  const uint32_t next = (s_rx_head + 1U) % RX_RING_SIZE;
  if (next != s_rx_tail) {
    s_rx_ring[s_rx_head] = data;
    s_rx_head = next;
  }
  pbl_sem_give(&s_rx_ready);
}

static bool prv_getc(uint8_t *c, uint32_t timeout_ms) {
  const RtcTicks end = rtc_get_ticks() + (timeout_ms * RTC_TICKS_HZ) / 1000U + 1U;

  while (s_rx_tail == s_rx_head) {
    const RtcTicks now = rtc_get_ticks();
    if (now >= end) {
      return false;
    }
    const uint32_t left_ms = (uint32_t)(((end - now) * 1000U) / RTC_TICKS_HZ) + 1U;
    (void)pbl_sem_take(&s_rx_ready, PBL_MSEC(left_ms));
  }

  *c = s_rx_ring[s_rx_tail];
  s_rx_tail = (s_rx_tail + 1U) % RX_RING_SIZE;
  return true;
}

static void prv_write(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uart_write_byte(BT_HCI_UART_TX, data[i]);
  }
}

static void prv_set_baud(uint32_t baud) {
  uart_wait_for_tx_complete(BT_HCI_UART_TX);
  uart_set_baud_rate(BT_HCI_UART_TX, baud);
  uart_set_baud_rate(BT_HCI_UART, baud);
}

static void prv_flush_rx(void) {
  pbl_irq_lock();
  s_rx_tail = s_rx_head;
  pbl_irq_unlock();
  (void)pbl_sem_take(&s_rx_ready, PBL_NO_WAIT);
}

static bool prv_wait_bootrom_start(void) {
  char id[BOOTROM_MAX_ID_LEN + 1] = {0};
  size_t id_len = 0;
  uint8_t first[2] = {0};
  uint32_t timeout_ms = BOOTROM_FIRST_CHAR_MS;

  for (size_t i = 0; i < BOOTROM_MAX_ID_LEN; i++) {
    uint8_t c;
    if (!prv_getc(&c, timeout_ms)) {
      PBL_LOG_ERR("No bootrom start (got %u chars)", (unsigned)i);
      sniprintf(s_status, sizeof(s_status), "Bootrom: none (%u ch %02x %02x)", (unsigned)i,
                first[0], first[1]);
      return false;
    }
    if (i < sizeof(first)) {
      first[i] = c;
    }
    if (c == STX) {
      PBL_LOG_INFO("DA1468x bootrom up, ID \"%s\"", id);
      strncpy(s_chip_id, id, sizeof(s_chip_id) - 1);
      return true;
    }
    // The lines glitch while the bootrom probes its interfaces, so only the
    // chip ID ("DA14681...") means it is up and sending.
    if (c == 'D') {
      timeout_ms = BOOTROM_CHAR_MS;
    }
    if (c >= 0x20U && c < 0x7fU && id_len < BOOTROM_MAX_ID_LEN) {
      id[id_len++] = (char)c;
    }
  }

  PBL_LOG_ERR("Bootrom sent no STX");
  sniprintf(s_status, sizeof(s_status), "Bootrom: no STX");
  return false;
}

static bool prv_bootrom_load_stage2(void) {
  const uint8_t *image = da1468x_stage2_start;
  const size_t len = da1468x_stage2_end - da1468x_stage2_start;
  const uint8_t header[] = {SOH, len & 0xffU, (len >> 8) & 0xffU};
  uint8_t c;

  prv_write(header, sizeof(header));
  if (!prv_getc(&c, BOOTROM_CHAR_MS) || c != ACK) {
    PBL_LOG_ERR("Bootrom refused header");
    sniprintf(s_status, sizeof(s_status), "Bootrom: header NAK");
    return false;
  }

  uint8_t xor = 0;
  for (size_t i = 0; i < len; i++) {
    xor ^= image[i];
  }
  prv_write(image, len);

  if (!prv_getc(&c, BOOTROM_CHAR_MS) || c != xor) {
    PBL_LOG_ERR("Bootrom checksum mismatch");
    sniprintf(s_status, sizeof(s_status), "Bootrom: bad checksum");
    return false;
  }
  prv_write(&(uint8_t){ACK}, 1);

  return true;
}

static void prv_put_u32(uint32_t v) {
  const uint8_t b[] = {v & 0xffU, (v >> 8) & 0xffU, (v >> 16) & 0xffU, (v >> 24) & 0xffU};
  prv_write(b, sizeof(b));
}

static bool prv_stage2_load_controller(void) {
  const uint8_t *image = da1468x_controller_start;
  const size_t len = da1468x_controller_end - da1468x_controller_start;
  uint8_t c;

  prv_set_baud(STAGE2_BAUD);
  pbl_thread_sleep(PBL_MSEC(STAGE2_START_MS));
  prv_flush_rx();

  prv_write((const uint8_t *)"PBL2", 4);
  prv_put_u32(len);
  prv_put_u32(pbl_crc32(0, image, len));
  if (!prv_getc(&c, STAGE2_REPLY_MS) || c != 'K') {
    PBL_LOG_ERR("Stage 2 not responding");
    sniprintf(s_status, sizeof(s_status), "Stage 2: no reply");
    return false;
  }

  prv_write(image, len);
  if (!prv_getc(&c, STAGE2_CRC_MS) || c != 'O') {
    PBL_LOG_ERR("Stage 2 rejected the controller image");
    sniprintf(s_status, sizeof(s_status), "Stage 2: bad image");
    return false;
  }

  return true;
}

static void prv_to_hex(char *out, const uint8_t *data, size_t n) {
  static const char digits[] = "0123456789abcdef";

  for (size_t i = 0; i < n; i++) {
    out[2 * i] = digits[data[i] >> 4];
    out[2 * i + 1] = digits[data[i] & 0xfU];
  }
  out[2 * n] = '\0';
}

// Collect the controller's init markers, to show how far it got, then what it
// sends once its HCI transport is up.
static void prv_collect_markers(void) {
  uint8_t after[RX_HEX_BYTES];
  size_t n = 0;
  uint8_t c;

  memset(s_markers, 0, sizeof(s_markers));
  s_rx_framing_errors = 0;
  s_rx_after_count = 0;
  while (n < MAX_MARKERS && prv_getc(&c, MARKERS_MS)) {
    s_markers[n++] = (c >= 0x20U && c < 0x7fU) ? (char)c : '?';
    if (c == MARKER_LAST) {
      break;
    }
  }

  while (s_rx_after_count < MAX_RX_AFTER && prv_getc(&c, MARKERS_HELLO_MS)) {
    if (s_rx_after_count < sizeof(after)) {
      after[s_rx_after_count] = c;
    }
    s_rx_after_count++;
  }
  prv_to_hex(s_rx_after_hex, after, MIN(s_rx_after_count, sizeof(after)));
}

// HCI Reset must complete before the host is let at the controller: a
// controller that runs but does not speak HCI would otherwise time the host
// start out, which crashes. Other bytes may come before the reply, so look
// for it in what arrives.
static bool prv_hci_probe(void) {
  static const uint8_t reset_cmd[] = {0x01, 0x03, 0x0c, 0x00};
  // Command Complete: 04 0e 04 <num_cmds> 03 0c <status>
  static const uint8_t reply[] = {0x04, 0x0e, 0x04, 0x00, 0x03, 0x0c, 0x00};
  uint8_t first[RX_HEX_BYTES];
  size_t n = 0;

  for (int attempt = 0; attempt < HCI_PROBE_ATTEMPTS; attempt++) {
    size_t matched = 0;
    uint8_t c;

    n = 0;
    prv_flush_rx();
    prv_write(reset_cmd, sizeof(reset_cmd));
    while (n < MAX_PROBE_RX && prv_getc(&c, HCI_PROBE_REPLY_MS)) {
      if (n < sizeof(first)) {
        first[n] = c;
      }
      n++;
      // Byte 3 is the number of commands the controller can take: any value.
      if (matched == 3 || c == reply[matched]) {
        matched++;
      } else {
        matched = (c == reply[0]) ? 1 : 0;
      }
      if (matched == sizeof(reply)) {
        return true;
      }
    }
  }

  PBL_LOG_ERR("Controller does not answer HCI Reset");
  prv_to_hex(s_rx_probe_hex, first, MIN(n, sizeof(first)));
  sniprintf(s_status, sizeof(s_status), "HCI [%s] %lu/%u f%lu", s_markers,
            (unsigned long)s_rx_after_count, (unsigned)n, (unsigned long)s_rx_framing_errors);
  return false;
}

static void prv_hold_in_reset(void) {
  pbl_gpio_configure(&BOARD_BT_RESET, PBL_GPIO_OUTPUT_ACTIVE);
}

static bool prv_boot_once(void) {
  prv_hold_in_reset();
  delay_us(100);
#ifdef BOARD_BT_HAS_FLOW_CONTROL
  // Released while the bootrom probes these pins for SPI.
  pbl_gpio_configure(&BOARD_BT_CTS, PBL_GPIO_INPUT);
  pbl_gpio_configure(&BOARD_BT_RTS, PBL_GPIO_INPUT);
#endif

  uart_init_rx_only(BT_HCI_UART);
  uart_init_tx_only(BT_HCI_UART_TX);
  prv_set_baud(BOOTROM_BAUD);
  prv_flush_rx();
  uart_set_rx_interrupt_handler(BT_HCI_UART, prv_rx_irq_handler);
  uart_set_rx_interrupt_enabled(BT_HCI_UART, true);

  pbl_gpio_set(&BOARD_BT_RESET, false);

  if (!prv_wait_bootrom_start() || !prv_bootrom_load_stage2() || !prv_stage2_load_controller()) {
    uart_set_rx_interrupt_enabled(BT_HCI_UART, false);
    uart_deinit(BT_HCI_UART);
    uart_deinit(BT_HCI_UART_TX);
    prv_hold_in_reset();
    return false;
  }

#ifdef BOARD_BT_HAS_FLOW_CONTROL
  // The controller image runs now: let it send.
  pbl_gpio_configure(&BOARD_BT_CTS, PBL_GPIO_OUTPUT_ACTIVE);
#endif
  prv_set_baud(CONFIG_BT_DA1468X_HCI_BAUD);
  prv_collect_markers();
  if (!prv_hci_probe()) {
    uart_set_rx_interrupt_enabled(BT_HCI_UART, false);
    uart_deinit(BT_HCI_UART);
    uart_deinit(BT_HCI_UART_TX);
    prv_hold_in_reset();
    return false;
  }

  uart_set_rx_interrupt_enabled(BT_HCI_UART, false);
  prv_flush_rx();
  return true;
}

bool da1468x_boot(void) {
  // STOP mode stops the USART clocks, and the threads sleep while waiting for
  // replies. Once the controller runs it can send at any time and its UART
  // has no flow control, so the block stays.
  soc_stm32_stop_block();

  // The bootrom times out between characters, so a preempted load can fail;
  // retry before giving up.
  for (int attempt = 1; attempt <= BOOT_ATTEMPTS; attempt++) {
    sniprintf(s_status, sizeof(s_status), "Booting (try %d)", attempt);
    if (prv_boot_once()) {
      PBL_LOG_INFO("DA1468x controller running (attempt %d)", attempt);
      sniprintf(s_status, sizeof(s_status), "Running (try %d)", attempt);
      return true;
    }
  }

  soc_stm32_stop_release();
  PBL_LOG_ERR("DA1468x boot failed, Bluetooth disabled");
  return false;
}

const char *da1468x_status(void) {
  return s_status;
}

bool da1468x_rts_ready(void) {
#ifdef BOARD_BT_HAS_FLOW_CONTROL
  return pbl_gpio_get(&BOARD_BT_RTS) > 0;
#else
  return true;
#endif
}

bool da1468x_has_wake(void) {
#ifdef BOARD_BT_HAS_WAKE
  return true;
#else
  return false;
#endif
}

bool da1468x_wake_pending(void) {
#ifdef BOARD_BT_HAS_WAKE
  return pbl_gpio_get(&BOARD_BT_WAKE) > 0;
#else
  return false;
#endif
}

void da1468x_wake_irq_init(ExtiHandlerCallback cb) {
#ifdef BOARD_BT_HAS_WAKE
  pbl_gpio_configure(&BOARD_BT_WAKE, PBL_GPIO_INPUT);
  const ExtiConfig config = pbl_exti_stm32_config(&BOARD_BT_WAKE);
  exti_configure_pin(config, ExtiTrigger_Rising, cb);
  exti_enable(config);
#endif
}

void da1468x_set_cts(bool allow) {
#ifdef BOARD_BT_HAS_FLOW_CONTROL
  pbl_gpio_set(&BOARD_BT_CTS, allow);
#endif
}

bool da1468x_has_flow_control(void) {
#ifdef BOARD_BT_HAS_FLOW_CONTROL
  // An image without flow control leaves the line to the pull-up.
  return da1468x_rts_ready();
#else
  return false;
#endif
}

const char *da1468x_chip_id(void) {
  return s_chip_id;
}

const char *da1468x_rx_after_markers(void) {
  return s_rx_after_hex;
}

const char *da1468x_rx_after_reset(void) {
  return s_rx_probe_hex;
}

/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! TI CC2564B as the H4 HCI controller (snowy).
//!
//! The chip runs from ROM, but needs TI's initialization script (patches and
//! settings) after every power up. The script is TI's TIInit_6.7.16.bts,
//! embedded unmodified and replayed here: each HCI command it holds is sent
//! and its Command Complete awaited. Its baud rate change is sent with our
//! rate, and its sleep mode setting with deep sleep off until the transport
//! supports eHCILL. The H4 transport takes the UART over afterwards.
//!
//! STOP mode stops the UART while RTS still tells the controller it may
//! send. Without BT_CC2564_SLEEP, STOP is blocked while the controller
//! runs; with it, the transport's eHCILL (TI SWRA288) lets STOP in while
//! the controller sleeps, holding RTS high meanwhile.
//!
//! Every wait is bounded and a failure leaves Bluetooth off instead of
//! crashing: a crash loop before the firmware is marked stable ends in the
//! stock bootloader starting PRF.

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <pbl/drivers/bluetooth/cc2564.h>
#include <pbl/drivers/exti.h>
#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/uart.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/stm32/sleep.h>
#include <pbl/util/math.h>

#include <board/board.h>
#include <stm32f4xx.h>

PBL_LOG_MODULE_DECLARE(nimble, CONFIG_NIMBLE_LOG_LEVEL);

#define BOOT_BAUD 115200U

#define H4_CMD 0x01U
#define H4_EVT 0x04U

#define HCILL_GO_TO_SLEEP_IND 0x30U

#define EVT_COMMAND_COMPLETE 0x0eU
#define EVT_COMMAND_STATUS   0x0fU

#define OP_READ_LOCAL_VERSION 0x1001U
#define OP_VS_UPDATE_BAUDRATE 0xff36U
#define OP_VS_SLEEP_MODE      0xfd0cU

#define BTS_MAGIC         0x42535442U // "BTSB"
#define BTS_HEADER_SIZE   32U
#define BTS_ACTION_SEND   1U
#define BTS_ACTION_WAIT   2U
#define BTS_ACTION_SERIAL 3U
#define BTS_ACTION_DELAY  4U

//! nSHUTDOWN low time per attempt. On hardware, after one of our resets the
//! chip stayed silent after 100 ms and 1 s off but started after 3 s.
static const uint32_t SHUTDOWN_PULSE_MS[] = {3000U, 5000U, 5000U};
//! While shut down the chip does not drive our CTS, which can read as clear
//! to send. Give it time to drive CTS high (busy) before waiting for low.
#define POWER_UP_SETTLE_MS 20U
//! After nSHUTDOWN rises the chip takes a few ms to start; it drives our CTS
//! low once it can take commands.
#define POWER_UP_TIMEOUT_MS 500U
#define CTS_TIMEOUT_MS      100U
#define CMD_TIMEOUT_MS      1000U
#define BOOT_ATTEMPTS       3

extern const uint8_t cc2564_bts_start[];
extern const uint8_t cc2564_bts_end[];

static char s_status[32] = "Not started";
static char s_chip_id[32] = "?";

// Start-up diagnostics: CTS while shut down and after the settle time, bytes
// received per attempt and the first few of attempt 1.
static int s_cts_shut;
static int s_cts_up;
static uint32_t s_rx_count[BOOT_ATTEMPTS];
static uint8_t s_rx_first[6];
static uint32_t s_rx_first_len;

// Boot-time receive: the UART interrupt fills a ring, the boot thread
// assembles events from it.
#define RX_RING_SIZE 512U
static uint8_t s_rx_ring[RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static PBL_SEM_DEFINE(s_rx_sem, 0, 1);

static uint8_t s_evt[260];
static uint16_t s_evt_len;
// The controller asked to sleep (eHCILL) before the transport took over.
static bool s_sleep_ind_pending;

static void prv_boot_rx_irq_handler(UARTDevice *dev, uint8_t data,
                                    const UARTRXErrorFlags *err_flags) {
  const uint32_t next = (s_rx_head + 1U) % RX_RING_SIZE;
  if (next != s_rx_tail) {
    s_rx_ring[s_rx_head] = data;
    s_rx_head = next;
  }
  pbl_sem_give(&s_rx_sem);
}

//! Feed received bytes into s_evt. @return true once it holds a whole event.
static bool prv_assemble_event(void) {
  while (s_rx_tail != s_rx_head) {
    const uint8_t c = s_rx_ring[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1U) % RX_RING_SIZE;
    if (s_evt_len == 0U && c != H4_EVT) {
      if (c == HCILL_GO_TO_SLEEP_IND) {
        s_sleep_ind_pending = true;
      }
      continue;
    }
    s_evt[s_evt_len++] = c;
    if (s_evt_len >= 3U && s_evt_len == 3U + s_evt[2]) {
      return true;
    }
  }
  return false;
}

//! Clear to send: the chip holds our CTS low.
static bool prv_wait_cts(uint32_t timeout_ms) {
  for (uint32_t ms = 0U; ms < timeout_ms; ms++) {
    if (pbl_gpio_get(&BOARD_BT_CTS) > 0) {
      return true;
    }
    pbl_thread_sleep(PBL_MSEC(1));
  }
  return pbl_gpio_get(&BOARD_BT_CTS) > 0;
}

static bool prv_send(const uint8_t *pkt, size_t len) {
  for (size_t i = 0U; i < len; i++) {
    // The UART would wait for CTS forever; a stuck chip must not hang us.
    if (!prv_wait_cts(CTS_TIMEOUT_MS)) {
      return false;
    }
    uart_write_byte(BT_HCI_UART, pkt[i]);
  }
  uart_wait_for_tx_complete(BT_HCI_UART);
  return true;
}

//! Wait for the Command Complete (or failed Command Status) of @p opcode.
//! @return The command's status, or -1 on timeout.
static int prv_wait_complete(uint16_t opcode, uint8_t *ret, size_t ret_len) {
  for (uint32_t waited = 0U; waited < CMD_TIMEOUT_MS;) {
    if (!prv_assemble_event()) {
      if (pbl_sem_take(&s_rx_sem, PBL_MSEC(10)) != 0) {
        waited += 10U;
      }
      continue;
    }

    const uint8_t code = s_evt[1];
    int status = -2;
    if (code == EVT_COMMAND_COMPLETE && s_evt_len >= 7U && (s_evt[4] | (s_evt[5] << 8)) == opcode) {
      status = s_evt[6];
      if (ret) {
        memcpy(ret, &s_evt[6], MIN(ret_len, (size_t)(s_evt_len - 6U)));
      }
    } else if (code == EVT_COMMAND_STATUS && s_evt_len >= 7U &&
               (s_evt[5] | (s_evt[6] << 8)) == opcode && s_evt[3] != 0U) {
      status = s_evt[3];
    }

    s_evt_len = 0U;
    if (status != -2) {
      return status;
    }
    // Some other event (e.g. vendor): keep waiting.
  }
  return -1;
}

static int prv_command(const uint8_t *pkt, size_t len, uint8_t *ret, size_t ret_len) {
  const uint16_t opcode = pkt[1] | (pkt[2] << 8);
  if (!prv_send(pkt, len)) {
    return -1;
  }
  return prv_wait_complete(opcode, ret, ret_len);
}

static void prv_power_cycle(int attempt) {
  pbl_gpio_set(&BOARD_BT_SHUTDOWN, true);
  pbl_thread_sleep(PBL_MSEC(SHUTDOWN_PULSE_MS[attempt - 1]));
  s_cts_shut = pbl_gpio_get(&BOARD_BT_CTS);
  pbl_gpio_set(&BOARD_BT_SHUTDOWN, false);
  pbl_thread_sleep(PBL_MSEC(POWER_UP_SETTLE_MS));
  s_cts_up = pbl_gpio_get(&BOARD_BT_CTS);
}

static void prv_uart_init(void) {
  uart_init(BT_HCI_UART);
  uart_set_baud_rate(BT_HCI_UART, BOOT_BAUD);
  s_evt_len = 0U;
  s_sleep_ind_pending = false;
  s_rx_head = 0U;
  s_rx_tail = 0U;
  (void)pbl_sem_take(&s_rx_sem, PBL_NO_WAIT);
  uart_set_rx_interrupt_handler(BT_HCI_UART, prv_boot_rx_irq_handler);
  uart_set_rx_interrupt_enabled(BT_HCI_UART, true);
}

static void prv_slow_clock_enable(void) {
  // MCO1 outputs the LSE: the chip's 32 kHz slow clock (and the FPGA's).
  pbl_gpio_stm32_af(&BOARD_MCO1, GPIO_AF_MCO, 0, GPIO_Speed_2MHz);
  RCC_MCO1Config(RCC_MCO1Source_LSE, RCC_MCO1Div_1);
}

static bool prv_read_version(void) {
  static const uint8_t cmd[] = {
    H4_CMD, OP_READ_LOCAL_VERSION & 0xffU, OP_READ_LOCAL_VERSION >> 8, 0
  };
  uint8_t ret[9];
  memset(ret, 0, sizeof(ret));
  if (prv_command(cmd, sizeof(cmd), ret, sizeof(ret)) != 0) {
    return false;
  }
  // status, HCI version, HCI revision (2), LMP version, manufacturer (2),
  // LMP subversion (2)
  const uint16_t manufacturer = ret[5] | (ret[6] << 8);
  const uint16_t subversion = ret[7] | (ret[8] << 8);
  sniprintf(s_chip_id, sizeof(s_chip_id), "HCI %u mfr %04x sub %04x", ret[1], manufacturer,
            subversion);
  PBL_LOG_INFO("CC2564: %s", s_chip_id);
  return true;
}

//! Replay the init script. @return false on the first failed command.
static bool prv_run_bts(void) {
  const uint8_t *p = cc2564_bts_start;
  const uint8_t *end = cc2564_bts_end;
  unsigned commands = 0U;

  if ((size_t)(end - p) < BTS_HEADER_SIZE ||
      (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) != BTS_MAGIC) {
    sniprintf(s_status, sizeof(s_status), "Bad init script");
    return false;
  }
  p += BTS_HEADER_SIZE;

  while (end - p >= 4) {
    const uint16_t type = p[0] | (p[1] << 8);
    const uint16_t len = p[2] | (p[3] << 8);
    const uint8_t *data = p + 4;
    if (end - data < len) {
      break;
    }
    p = data + len;

    if (type == BTS_ACTION_DELAY && len >= 4U) {
      pbl_thread_sleep(
          PBL_MSEC(data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24)));
      continue;
    }
    if (type != BTS_ACTION_SEND || len < 4U || data[0] != H4_CMD) {
      // Waits (we await each command ourselves), serial settings and remarks
      continue;
    }

    const uint16_t opcode = data[1] | (data[2] << 8);
    uint8_t pkt[260];
    memcpy(pkt, data, MIN((size_t)len, sizeof(pkt)));

    if (opcode == OP_VS_UPDATE_BAUDRATE && len >= 8U) {
      const uint32_t baud = CONFIG_BT_CC2564_HCI_BAUD;
      memcpy(&pkt[4], &baud, sizeof(baud));
    }
#ifndef CONFIG_BT_CC2564_SLEEP
    else if (opcode == OP_VS_SLEEP_MODE && len >= 6U) {
      // Deep sleep (eHCILL) off; big sleep as the script sets it.
      pkt[5] = 0U;
    }
#endif

    const int status = prv_command(pkt, MIN((size_t)len, sizeof(pkt)), nullptr, 0U);
    if (status != 0) {
      sniprintf(s_status, sizeof(s_status), "Init %u %04x:%d", commands, opcode, status);
      return false;
    }
    commands++;

    if (opcode == OP_VS_UPDATE_BAUDRATE) {
      // The reply came at the old rate; both sides switch now.
      uart_set_baud_rate(BT_HCI_UART, CONFIG_BT_CC2564_HCI_BAUD);
      pbl_thread_sleep(PBL_MSEC(5));
    }
  }

  PBL_LOG_INFO("CC2564: init script done, %u commands", commands);
  return true;
}

bool cc2564_boot(void) {
  pbl_gpio_configure(&BOARD_BT_SHUTDOWN, PBL_GPIO_OUTPUT_ACTIVE);
  prv_slow_clock_enable();

  for (int attempt = 1; attempt <= BOOT_ATTEMPTS; attempt++) {
    prv_uart_init();
    prv_power_cycle(attempt);

    if (!prv_wait_cts(POWER_UP_TIMEOUT_MS)) {
      sniprintf(s_status, sizeof(s_status), "No CTS (try %d)", attempt);
      continue;
    }
    if (!prv_read_version()) {
      // None received means the chip is silent, others a baud rate mismatch.
      s_rx_count[attempt - 1] = s_rx_head;
      if (attempt == 1) {
        s_rx_first_len = MIN(s_rx_head, sizeof(s_rx_first));
        memcpy(s_rx_first, s_rx_ring, s_rx_first_len);
      }
      sniprintf(s_status, sizeof(s_status), "No version t%d rx %lu,%lu,%lu c%d%d", attempt,
                (unsigned long)s_rx_count[0], (unsigned long)s_rx_count[1],
                (unsigned long)s_rx_count[2], s_cts_shut, s_cts_up);
      char *p = s_chip_id;
      const char *end = s_chip_id + sizeof(s_chip_id);
      p += sniprintf(p, end - p, "a1");
      for (uint32_t i = 0U; i < s_rx_first_len; i++) {
        p += sniprintf(p, end - p, "%02x", s_rx_first[i]);
      }
      // Line levels (RX, TX, CTS, RTS on PA10, PA9, PA11, PA12) and the
      // USART status: an idle RX is high if the chip drives its TX.
      const uint32_t idr = GPIOA->IDR;
      sniprintf(p, end - p, " L%lu%lu%lu%lu s%03lx", (unsigned long)((idr >> 10) & 1U),
                (unsigned long)((idr >> 9) & 1U), (unsigned long)((idr >> 11) & 1U),
                (unsigned long)((idr >> 12) & 1U),
                (unsigned long)(BOARD_BT_HCI_USART->SR & 0x3ffU));
      continue;
    }
    if (!prv_run_bts()) {
      continue;
    }

    uart_set_rx_interrupt_enabled(BT_HCI_UART, false);
    // Held while the controller is awake; the transport's eHCILL releases it.
    soc_stm32_stop_block();
    sniprintf(s_status, sizeof(s_status), "Running (try %d) c%d%d", attempt, s_cts_shut, s_cts_up);
    return true;
  }

  // Leave the chip off.
  uart_set_rx_interrupt_enabled(BT_HCI_UART, false);
  pbl_gpio_set(&BOARD_BT_SHUTDOWN, true);
  PBL_LOG_ERR("CC2564 failed to start: %s", s_status);
  return false;
}

bool cc2564_take_sleep_ind(void) {
  const bool pending = s_sleep_ind_pending;
  s_sleep_ind_pending = false;
  return pending;
}

void cc2564_hold_rts(bool hold) {
  if (hold) {
    // High: the controller must not send.
    pbl_gpio_configure(&BOARD_BT_HOST_RTS, PBL_GPIO_OUTPUT_HIGH);
  } else {
    pbl_gpio_stm32_af(&BOARD_BT_HOST_RTS, GPIO_AF_USART1, 0, GPIO_Speed_50MHz);
  }
}

void cc2564_cts_wake_enable(ExtiHandlerCallback cb) {
  // The controller pulses its RTS (our CTS) to wake us.
  const ExtiConfig exti = pbl_exti_stm32_config(&BOARD_BT_CTS);
  exti_configure_pin(exti, ExtiTrigger_RisingFalling, cb);
  exti_enable(exti);
}

void cc2564_send_hcill(uint8_t msg) {
  // The controller may hold our CTS deasserted while it sleeps; a wake-up
  // message must get through anyway.
  USART_TypeDef *usart = BOARD_BT_HCI_USART;
  const bool cts_ok = prv_wait_cts(10U);
  if (!cts_ok) {
    usart->CR3 &= ~USART_CR3_CTSE;
  }
  uart_write_byte(BT_HCI_UART, msg);
  uart_wait_for_tx_complete(BT_HCI_UART);
  if (!cts_ok) {
    usart->CR3 |= USART_CR3_CTSE;
  }
}

void cc2564_cts_wake_disable(void) {
  exti_disable(pbl_exti_stm32_config(&BOARD_BT_CTS));
}

void cc2564_shutdown(void) {
  // A plain write: the pin is an output once cc2564_boot() has run.
  pbl_gpio_set(&BOARD_BT_SHUTDOWN, true);
}

const char *cc2564_status(void) {
  return s_status;
}

const char *cc2564_chip_id(void) {
  return s_chip_id;
}

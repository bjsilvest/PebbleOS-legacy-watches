/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/uart.h>

#include <board/board.h>
#include <stm32f4xx.h>

typedef struct UARTDeviceState {
  /** Initialized. */
  bool initialized;
  /** Receive handler. */
  UARTRXInterruptHandler rx_irq_handler;
  /** Transmit handler. */
  UARTTXInterruptHandler tx_irq_handler;
  /** Receive interrupt enabled. */
  bool rx_int_enabled;
  /** Transmit interrupt enabled. */
  bool tx_int_enabled;
} UARTDeviceState;

struct UARTDevice {
  /** Driver state. */
  UARTDeviceState *state;
  /** Peripheral. */
  USART_TypeDef *periph;
  /** RCC APB enable bit (RCC_APB1Periph_* or RCC_APB2Periph_*). */
  uint32_t rcc_apb_periph;
  /** On APB2 (USART1/6) rather than APB1. */
  bool apb2;
  /** TX pin, not connected if unused. */
  struct pbl_gpio tx;
  /** RX pin, not connected if unused. */
  struct pbl_gpio rx;
  /** Alternate function of the pins (GPIO_AF_USARTx). */
  uint8_t af;
  /** Interrupt number. */
  int irqn;
};

/* UART interrupt handler, connected with PBL_IRQ_CONNECT() in the board file. */
void uart_irq_handler(UARTDevice *dev);

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/uart/stm32.h>
#include <pbl/kernel/irq.h>

#include <board/board.h>

// Debug UART
///////////////////////////////////////////////////////////////////////////////

static UARTDeviceState s_dbg_uart_state;
static UARTDevice DBG_UART_DEVICE = {
  .state = &s_dbg_uart_state,
  .periph = USART3,
  .rcc_apb_periph = RCC_APB1Periph_USART3,
  .tx = PBL_GPIO(STM32_GPIOC, 10, 0),
  .rx = PBL_GPIO(STM32_GPIOC, 11, 0),
  .af = GPIO_AF_USART3,
  .irqn = PBL_IRQN(USART3),
};
UARTDevice *const DBG_UART = &DBG_UART_DEVICE;
PBL_IRQ_CONNECT(USART3, 13, uart_irq_handler, DBG_UART, 0);

void board_early_init(void) {
  // The buttons' common line
  pbl_gpio_configure(&BOARD_BUTTON_COM, PBL_GPIO_OUTPUT_INACTIVE);
}

void board_init(void) {
}

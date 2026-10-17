/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/i2c/stm32.h>
#include <pbl/drivers/regulator/gpio.h>
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
#ifdef CONFIG_BOARD_TINTIN_V1_5
  .tx = PBL_GPIO(STM32_GPIOD, 8, 0),
  .rx = PBL_GPIO(STM32_GPIOD, 9, 0),
#else
  .tx = PBL_GPIO(STM32_GPIOC, 10, 0),
  .rx = PBL_GPIO(STM32_GPIOC, 11, 0),
#endif
  .af = GPIO_AF_USART3,
  .irqn = PBL_IRQN(USART3),
};
UARTDevice *const DBG_UART = &DBG_UART_DEVICE;
PBL_IRQ_CONNECT(USART3, 13, uart_irq_handler, DBG_UART, 0);

#ifdef CONFIG_BT_HCI_UART
// Bluetooth HCI
///////////////////////////////////////////////////////////////////////////////

// Hardware flow control holds the controller off while a byte is unread, so
// RX can stay interrupt driven.
static UARTDeviceState s_bt_hci_uart_state;
static UARTDevice BT_HCI_UART_DEVICE = {
  .state = &s_bt_hci_uart_state,
  .periph = USART1,
  .rcc_apb_periph = RCC_APB2Periph_USART1,
  .apb2 = true,
  .tx = PBL_GPIO(STM32_GPIOA, 9, 0),
  .rx = PBL_GPIO(STM32_GPIOA, 10, 0),
  .cts = PBL_GPIO(STM32_GPIOA, 11, 0),
  .rts = PBL_GPIO(STM32_GPIOA, 12, 0),
  .af = GPIO_AF_USART1,
  .irqn = PBL_IRQN(USART1),
};
UARTDevice *const BT_HCI_UART = &BT_HCI_UART_DEVICE;
PBL_IRQ_CONNECT(USART1, 12, uart_irq_handler, BT_HCI_UART, 0);
#endif

// I2C1: accelerometer
///////////////////////////////////////////////////////////////////////////////

PBL_I2C_STM32_DEFINE(s_i2c1, "i2c1", I2C1, RCC_APB1Periph_I2C1, 400000, PBL_GPIO(STM32_GPIOB, 8, 0),
                     GPIO_AF_I2C1, PBL_GPIO(STM32_GPIOB, 7, 0), GPIO_AF_I2C1, nullptr);
PBL_IRQ_CONNECT(I2C1_EV, 12, pbl_i2c_stm32_ev_irq_handler, &s_i2c1, 0);
PBL_IRQ_CONNECT(I2C1_ER, 12, pbl_i2c_stm32_er_irq_handler, &s_i2c1, 0);

static const struct pbl_i2c_dev I2C_SLAVE_LIS3DH = PBL_I2C_DEV(&s_i2c1.bus, 0x19);
const struct pbl_i2c_dev *const I2C_LIS3DH = &I2C_SLAVE_LIS3DH;

// I2C2: magnetometer (and the unused MFi chip), on a switched 2.5 V rail
///////////////////////////////////////////////////////////////////////////////

PBL_GPIO_REGULATOR_DEFINE(BOARD_RAIL_2V5, "rail_2v5", STM32_GPIOH, 0, 0, false);

PBL_I2C_STM32_DEFINE(s_i2c2, "i2c2", I2C2, RCC_APB1Periph_I2C2, 400000,
                     PBL_GPIO(STM32_GPIOB, 10, 0), GPIO_AF_I2C2, PBL_GPIO(STM32_GPIOB, 11, 0),
                     GPIO_AF_I2C2, nullptr);
PBL_IRQ_CONNECT(I2C2_EV, 12, pbl_i2c_stm32_ev_irq_handler, &s_i2c2, 0);
PBL_IRQ_CONNECT(I2C2_ER, 12, pbl_i2c_stm32_er_irq_handler, &s_i2c2, 0);

static const struct pbl_i2c_dev I2C_SLAVE_MAG3110 = PBL_I2C_DEV(&s_i2c2.bus, 0x0e);
const struct pbl_i2c_dev *const I2C_MAG3110 = &I2C_SLAVE_MAG3110;

void board_early_init(void) {
  // The buttons' common line
  pbl_gpio_configure(&BOARD_BUTTON_COM, PBL_GPIO_OUTPUT_INACTIVE);
}

void board_init(void) {
}

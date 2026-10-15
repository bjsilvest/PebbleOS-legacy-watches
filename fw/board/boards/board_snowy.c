/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/dma/stm32.h>
#include <pbl/drivers/i2c/stm32.h>
#include <pbl/drivers/mic/stm32/i2s_pdm_definitions.h>
#include <pbl/drivers/pmic/max14690.h>
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

// I2C1: PMIC and compass
///////////////////////////////////////////////////////////////////////////////

PBL_I2C_STM32_DEFINE(s_i2c1, "i2c1", I2C1, RCC_APB1Periph_I2C1, 400000, PBL_GPIO(STM32_GPIOB, 6, 0),
                     GPIO_AF_I2C1, PBL_GPIO(STM32_GPIOB, 9, 0), GPIO_AF_I2C1, nullptr);
PBL_IRQ_CONNECT(I2C1_EV, 12, pbl_i2c_stm32_ev_irq_handler, &s_i2c1, 0);
PBL_IRQ_CONNECT(I2C1_ER, 12, pbl_i2c_stm32_er_irq_handler, &s_i2c1, 0);

static const struct pbl_i2c_dev I2C_SLAVE_MAX14690 = PBL_I2C_DEV(&s_i2c1.bus, 0x28);

// PMIC
///////////////////////////////////////////////////////////////////////////////

// MON output on ADC channel 1 (PA1)
PBL_MAX14690_DEFINE(s_max14690, &s_i2c1.bus, &I2C_SLAVE_MAX14690,
                    .int_gpio = PBL_GPIO(STM32_GPIOG, 7, 0), .mon_adc_channel = ADC_Channel_1,
                    .mon_adc_pin = PBL_GPIO(STM32_GPIOA, 1, 0));
const struct pbl_max14690 *const MAX14690 = &s_max14690;

// Microphone: I2S on SPI2
///////////////////////////////////////////////////////////////////////////////

// SPI2_RX: DMA1 stream 3, channel 0
static struct pbl_dma_stm32_state s_mic_dma_state;
static const struct pbl_dma_stm32_stream MIC_DMA = {
  .state = &s_mic_dma_state,
  .stream = DMA1_Stream3,
  .channel = DMA_Channel_0,
  .rcc = RCC_AHB1Periph_DMA1,
  .it_ht = DMA_IT_HTIF3,
  .it_tc = DMA_IT_TCIF3,
  .irqn = PBL_IRQN(DMA1_Stream3),
};
PBL_IRQ_CONNECT(DMA1_Stream3, 15, pbl_dma_stm32_irq_handler, &MIC_DMA, 0);

static MicDeviceState s_mic_state;
static MicDevice MIC_DEVICE = {
  .state = &s_mic_state,
  .spi = SPI2,
  .spi_rcc = RCC_APB1Periph_SPI2,
  .dma = &MIC_DMA,
  .ck_gpio = PBL_GPIO(STM32_GPIOB, 10, 0),
  .sd_gpio = PBL_GPIO(STM32_GPIOB, 15, 0),
  .gpio_af = GPIO_AF_SPI2,
  .default_volume = 250,
};
MicDevice *const MIC = &MIC_DEVICE;

void board_early_init(void) {
}

void board_init(void) {
}

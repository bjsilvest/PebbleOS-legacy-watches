/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/dma/stm32.h>
#include <pbl/drivers/flash/qspi_flash_definitions.h>
#include <pbl/drivers/i2c/stm32.h>
#include <pbl/drivers/mic/stm32/dfsdm_definitions.h>
#include <pbl/drivers/pmic/as3701b.h>
#include <pbl/drivers/uart/stm32.h>
#include <pbl/kernel/irq.h>

#include <board/board.h>
#include <flash_region/flash_region.h>

// Debug UART
///////////////////////////////////////////////////////////////////////////////

static UARTDeviceState s_dbg_uart_state;
static UARTDevice DBG_UART_DEVICE = {
  .state = &s_dbg_uart_state,
  .periph = USART1,
  .rcc_apb_periph = RCC_APB2Periph_USART1,
  .apb2 = true,
  .tx = PBL_GPIO(STM32_GPIOA, 9, 0),
  .rx = PBL_GPIO(STM32_GPIOB, 7, 0),
  .af = GPIO_AF_USART1,
  .irqn = PBL_IRQN(USART1),
};
UARTDevice *const DBG_UART = &DBG_UART_DEVICE;
PBL_IRQ_CONNECT(USART1, 13, uart_irq_handler, DBG_UART, 0);

#ifdef CONFIG_BT_HCI_UART
// Bluetooth HCI
///////////////////////////////////////////////////////////////////////////////

static UARTDeviceState s_bt_hci_rx_uart_state;
static UARTDevice BT_HCI_RX_UART_DEVICE = {
  .state = &s_bt_hci_rx_uart_state,
  .periph = USART6,
  .rcc_apb_periph = RCC_APB2Periph_USART6,
  .apb2 = true,
  .rx = PBL_GPIO(STM32_GPIOA, 12, 0),
  .af = GPIO_AF_USART6,
  .irqn = PBL_IRQN(USART6),
};
UARTDevice *const BT_HCI_UART = &BT_HCI_RX_UART_DEVICE;
PBL_IRQ_CONNECT(USART6, 12, uart_irq_handler, BT_HCI_UART, 0);

static UARTDeviceState s_bt_hci_tx_uart_state;
static UARTDevice BT_HCI_TX_UART_DEVICE = {
  .state = &s_bt_hci_tx_uart_state,
  .periph = USART2,
  .rcc_apb_periph = RCC_APB1Periph_USART2,
  .tx = PBL_GPIO(STM32_GPIOA, 2, 0),
  .af = GPIO_AF_USART2,
  .irqn = PBL_IRQN(USART2),
};
UARTDevice *const BT_HCI_TX_UART = &BT_HCI_TX_UART_DEVICE;
#endif

// QSPI flash
///////////////////////////////////////////////////////////////////////////////

static QSPIPortState s_qspi_port_state;
static QSPIPort QSPI_PORT = {
  .state = &s_qspi_port_state,
  .clock_speed_hz = 50000000,
  .auto_polling_interval = 16,
  .cs_gpio = PBL_GPIO(STM32_GPIOB, 6, 0),
  .cs_af = GPIO_AF10_QUADSPI,
  .clk_gpio = PBL_GPIO(STM32_GPIOB, 2, 0),
  .clk_af = GPIO_AF9_QUADSPI,
  .data_gpio =
      {
        PBL_GPIO(STM32_GPIOC, 9, 0),
        PBL_GPIO(STM32_GPIOC, 10, 0),
        PBL_GPIO(STM32_GPIOC, 8, 0),
        PBL_GPIO(STM32_GPIOA, 1, 0),
      },
  .data_af = GPIO_AF9_QUADSPI,
  .flash_size = BOARD_NOR_FLASH_SIZE,
};
QSPIPort *const QSPI = &QSPI_PORT;

static QSPIFlashState s_qspi_flash_state;
static QSPIFlash QSPI_FLASH_DEVICE = {
  .state = &s_qspi_flash_state,
  .qspi = &QSPI_PORT,
  .default_fast_read_ddr_enabled = false,
};
QSPIFlash *const QSPI_FLASH = &QSPI_FLASH_DEVICE;

// I2C3: PMIC (and HRM on Pebble 2 HR)
///////////////////////////////////////////////////////////////////////////////

PBL_I2C_STM32_DEFINE(s_i2c3, "i2c3", I2C3, RCC_APB1Periph_I2C3, 400000, PBL_GPIO(STM32_GPIOA, 8, 0),
                     GPIO_AF_I2C3, PBL_GPIO(STM32_GPIOB, 8, 0), GPIO_AF9_I2C3, nullptr);
PBL_IRQ_CONNECT(I2C3_EV, 12, pbl_i2c_stm32_ev_irq_handler, &s_i2c3, 0);
PBL_IRQ_CONNECT(I2C3_ER, 12, pbl_i2c_stm32_er_irq_handler, &s_i2c3, 0);

static const struct pbl_i2c_dev I2C_SLAVE_AS3701B = PBL_I2C_DEV(&s_i2c3.bus, 0x40);

// PMIC
///////////////////////////////////////////////////////////////////////////////

PBL_AS3701B_DEFINE(s_as3701b, &s_i2c3.bus, &I2C_SLAVE_AS3701B,
                   .int_gpio = PBL_GPIO(STM32_GPIOC, 7, 0), .vbat_adc_channel = ADC_Channel_5,
                   .vbat_adc_pin = PBL_GPIO(STM32_GPIOA, 5, 0),
                   // Battery divider: R13 47k over R15 30.1k
                   .vbat_scale_numerator = 771, .vbat_scale_denominator = 301);
const struct pbl_as3701b *const AS3701B = &s_as3701b;

// Microphone
///////////////////////////////////////////////////////////////////////////////

static struct pbl_dma_stm32_state s_dfsdm_dma_state;
static const struct pbl_dma_stm32_stream DFSDM_DMA = {
  .state = &s_dfsdm_dma_state,
  .stream = DMA2_Stream6,
  .channel = DMA_Channel_3,
  .rcc = RCC_AHB1Periph_DMA2,
  .it_ht = DMA_IT_HTIF6,
  .it_tc = DMA_IT_TCIF6,
  .irqn = PBL_IRQN(DMA2_Stream6),
};
PBL_IRQ_CONNECT(DMA2_Stream6, 13, pbl_dma_stm32_irq_handler, &DFSDM_DMA, 0);

static MicDeviceState s_mic_state;
static MicDevice MIC_DEVICE = {
  .state = &s_mic_state,
  .filter = (DFSDM_TypeDef *)DFSDM1_Filter0_BASE,
  .channel = DFSDM1_Channel2,
  .regular_channel = DFSDM_RegularChannel2,
  .pdm_frequency = 2000000,
  .rcc_apb_periph = RCC_APB2Periph_DFSDM,
  .dma = &DFSDM_DMA,
  .ck_gpio = PBL_GPIO(STM32_GPIOC, 2, 0),
  .sd_gpio = PBL_GPIO(STM32_GPIOB, 14, 0),
  .gpio_af = GPIO_AF8_DFSDM1,
  .power_on_delay_ms = 50,
  .settling_delay_ms = 0,
  .default_volume = 64,
  .final_right_shift = 11,
};
MicDevice *const MIC = &MIC_DEVICE;

void board_early_init(void) {
}

void board_init(void) {
}

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! BMA255 on silk's SPI3 (SCLK PB12, MISO PC11, MOSI PC12, SCS PA4 active low).

#include "bma255_private.h"
#include "bma255_regs.h"

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/delay.h>

#include <stm32f4xx.h>

#define BMA255_SPI SPI3

static const struct pbl_gpio s_sclk = PBL_GPIO(STM32_GPIOB, 12, 0);
static const struct pbl_gpio s_miso = PBL_GPIO(STM32_GPIOC, 11, 0);
static const struct pbl_gpio s_mosi = PBL_GPIO(STM32_GPIOC, 12, 0);
static const struct pbl_gpio s_scs = PBL_GPIO(STM32_GPIOA, 4, PBL_GPIO_ACTIVE_LOW);

// Wait at least 450 us after a write. One tick is under that, so wait for the
// tick counter to advance twice.
#define MIN_TICKS_AFTER_WRITE 2
static RtcTicks s_last_write_ticks;
static_assert(RTC_TICKS_HZ < (1000000 / 450), "Tick period < 450us");

void bma255_gpio_init(void) {
  pbl_gpio_stm32_af(&s_sclk, GPIO_AF7_SPI3, 0, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&s_miso, GPIO_AF_SPI3, 0, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&s_mosi, GPIO_AF_SPI3, 0, GPIO_Speed_50MHz);
  pbl_gpio_configure(&s_scs, PBL_GPIO_OUTPUT_INACTIVE);

  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);

  // The BMA255 allows up to 10 MHz; run at no more than 5 MHz.
  static const uint16_t prescalers[] = {
    SPI_BaudRatePrescaler_2,   SPI_BaudRatePrescaler_4,   SPI_BaudRatePrescaler_8,
    SPI_BaudRatePrescaler_16,  SPI_BaudRatePrescaler_32,  SPI_BaudRatePrescaler_64,
    SPI_BaudRatePrescaler_128, SPI_BaudRatePrescaler_256,
  };
  uint16_t prescaler = SPI_BaudRatePrescaler_256;
  for (unsigned i = 0; i < sizeof(prescalers) / sizeof(prescalers[0]); i++) {
    if ((clocks.PCLK1_Frequency >> (i + 1)) <= 5000000) {
      prescaler = prescalers[i];
      break;
    }
  }

  SPI_InitTypeDef spi_cfg;
  SPI_StructInit(&spi_cfg);
  spi_cfg.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  spi_cfg.SPI_Mode = SPI_Mode_Master;
  spi_cfg.SPI_DataSize = SPI_DataSize_8b;
  spi_cfg.SPI_CPOL = SPI_CPOL_Low;
  spi_cfg.SPI_CPHA = SPI_CPHA_1Edge;
  spi_cfg.SPI_NSS = SPI_NSS_Soft;
  spi_cfg.SPI_BaudRatePrescaler = prescaler;
  spi_cfg.SPI_FirstBit = SPI_FirstBit_MSB;
  spi_cfg.SPI_CRCPolynomial = 7;

  bma255_enable_spi_clock();
  SPI_I2S_DeInit(BMA255_SPI);
  SPI_Init(BMA255_SPI, &spi_cfg);
  SPI_Cmd(BMA255_SPI, ENABLE);
  bma255_disable_spi_clock();
}

void bma255_enable_spi_clock(void) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI3, ENABLE);
}

void bma255_disable_spi_clock(void) {
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI3, DISABLE);
}

uint8_t bma255_send_and_receive_byte(uint8_t byte) {
  while (SPI_I2S_GetFlagStatus(BMA255_SPI, SPI_I2S_FLAG_TXE) == RESET) {
  }
  SPI_I2S_SendData(BMA255_SPI, byte);
  while (SPI_I2S_GetFlagStatus(BMA255_SPI, SPI_I2S_FLAG_RXNE) == RESET) {
  }
  return SPI_I2S_ReceiveData(BMA255_SPI);
}

void bma255_send_byte(uint8_t byte) {
  while (SPI_I2S_GetFlagStatus(BMA255_SPI, SPI_I2S_FLAG_TXE) == RESET) {
  }
  SPI_I2S_SendData(BMA255_SPI, byte);
}

//! Assert SCS, start the SPI clock and send the address.
void bma255_prepare_txn(uint8_t address) {
  while (rtc_get_ticks() < (s_last_write_ticks + MIN_TICKS_AFTER_WRITE)) {
    pbl_sleep_ms(1);
  }
  pbl_gpio_set(&s_scs, true);
  bma255_enable_spi_clock();
  bma255_send_and_receive_byte(address);
}

void bma255_end_txn(void) {
  bma255_disable_spi_clock();
  pbl_gpio_set(&s_scs, false);
}

void bma255_burst_read(uint8_t address, uint8_t *data, size_t len) {
  bma255_prepare_txn(address | BMA255_READ_FLAG);
  for (size_t i = 0; i < len; ++i) {
    data[i] = bma255_send_and_receive_byte(0);
  }
  bma255_end_txn();
}

uint8_t bma255_read_register(uint8_t address) {
  bma255_prepare_txn(address | BMA255_READ_FLAG);
  const uint8_t data = bma255_send_and_receive_byte(0);
  bma255_end_txn();
  return data;
}

void bma255_write_register(uint8_t address, uint8_t data) {
  bma255_prepare_txn(address | BMA255_WRITE_FLAG);
  bma255_send_and_receive_byte(data);
  bma255_end_txn();
  s_last_write_ticks = rtc_get_ticks();
}

void bma255_read_modify_write(uint8_t reg, uint8_t value, uint8_t mask) {
  uint8_t new_val = bma255_read_register(reg);
  new_val &= ~mask;
  new_val |= value;
  bma255_write_register(reg, new_val);
}

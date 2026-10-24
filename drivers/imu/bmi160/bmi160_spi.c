/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! BMI160 on snowy's SPI1 (SCK PA5, MISO PA6, MOSI PA7, SCS PA4 active low).

#include "bmi160_private.h"
#include "bmi160_regs.h"

#include <pbl/drivers/gpio/stm32.h>

#include <pbl/kernel/delay.h>
#include <stm32f4xx.h>

#define BMI160_SPI          SPI1
#define BMI160_SPI_CLOCK_HZ 5000000

static const struct pbl_gpio s_sclk = PBL_GPIO(STM32_GPIOA, 5, 0);
static const struct pbl_gpio s_miso = PBL_GPIO(STM32_GPIOA, 6, 0);
static const struct pbl_gpio s_mosi = PBL_GPIO(STM32_GPIOA, 7, 0);
static const struct pbl_gpio s_scs = PBL_GPIO(STM32_GPIOA, 4, PBL_GPIO_ACTIVE_LOW);

void bmi160_spi_init(void) {
  pbl_gpio_stm32_af(&s_sclk, GPIO_AF_SPI1, 0, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&s_miso, GPIO_AF_SPI1, 0, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&s_mosi, GPIO_AF_SPI1, 0, GPIO_Speed_50MHz);
  pbl_gpio_configure(&s_scs, PBL_GPIO_OUTPUT_INACTIVE);

  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);

  // SPI1 is on APB2. Pick the smallest divider not above the target.
  static const uint16_t prescalers[] = {
    SPI_BaudRatePrescaler_2,   SPI_BaudRatePrescaler_4,   SPI_BaudRatePrescaler_8,
    SPI_BaudRatePrescaler_16,  SPI_BaudRatePrescaler_32,  SPI_BaudRatePrescaler_64,
    SPI_BaudRatePrescaler_128, SPI_BaudRatePrescaler_256,
  };
  uint16_t prescaler = SPI_BaudRatePrescaler_256;
  for (unsigned i = 0; i < sizeof(prescalers) / sizeof(prescalers[0]); i++) {
    if ((clocks.PCLK2_Frequency >> (i + 1)) <= BMI160_SPI_CLOCK_HZ) {
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

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
  SPI_I2S_DeInit(BMI160_SPI);
  SPI_Init(BMI160_SPI, &spi_cfg);
  SPI_Cmd(BMI160_SPI, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, DISABLE);
}

static uint8_t prv_send_and_receive_byte(uint8_t byte) {
  while (SPI_I2S_GetFlagStatus(BMI160_SPI, SPI_I2S_FLAG_TXE) == RESET) {
  }
  SPI_I2S_SendData(BMI160_SPI, byte);
  while (SPI_I2S_GetFlagStatus(BMI160_SPI, SPI_I2S_FLAG_RXNE) == RESET) {
  }
  return SPI_I2S_ReceiveData(BMI160_SPI);
}

void bmi160_begin_burst(uint8_t addr) {
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
  pbl_gpio_set(&s_scs, true);
  prv_send_and_receive_byte(addr);
}

void bmi160_burst_read(void *buf, size_t len) {
  uint8_t *out = buf;
  for (size_t i = 0; i < len; i++) {
    out[i] = prv_send_and_receive_byte(0);
  }
}

void bmi160_end_burst(void) {
  pbl_gpio_set(&s_scs, false);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, DISABLE);
}

uint8_t bmi160_read_reg(uint8_t reg) {
  uint8_t value;
  // Registers are read when the address MSB=1
  bmi160_begin_burst(reg | BMI160_READ_FLAG);
  bmi160_burst_read(&value, 1);
  bmi160_end_burst();
  return value;
}

uint16_t bmi160_read_16bit_reg(uint8_t reg) {
  // 16-bit registers are little endian
  uint8_t value[2];
  bmi160_begin_burst(reg | BMI160_READ_FLAG);
  bmi160_burst_read(value, sizeof(value));
  bmi160_end_burst();
  return value[0] | (value[1] << 8);
}

void bmi160_write_reg(uint8_t reg, uint8_t value) {
  bmi160_begin_burst(reg & BMI160_REG_MASK);
  prv_send_and_receive_byte(value);
  bmi160_end_burst();
}

void bmi160_enable_spi_mode(void) {
  // The BMI160 needs a rising edge on SCS to switch into SPI mode; the
  // datasheet recommends reading the reserved register 0x7F.
  bmi160_read_reg(0x7f);
  pbl_sleep_ms(2); // Necessary on cold boots
}

/* SPDX-FileCopyrightText: 2025 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Sharp LS013B7DH01 on an STM32F4 SPI, write-only and LSB first.
//!
//! On silk the panel's VCOM (EXTCOMIN) is driven by the PMIC, not the MCU.
//! Transfers are polled for now; completion is still reported through a
//! kernel event, as with a DMA transfer.

#include <stdint.h>
#include <string.h>

#include <pbl/drivers/display/sharp_ls013b7dh01/sharp_ls013b7dh01.h>
#include <pbl/drivers/gpio/stm32.h>
#include <pbl/util/bitops.h>

#include <board/board.h>
#include <kernel/events.h>
#include <stm32f4xx.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

#define DISP_MODE_WRITE 0x01U
#define DISP_MODE_CLEAR 0x04U

#define DISP_SPI_CLOCK_HZ 2000000

static uint8_t s_buf[2 + ((DISP_LINE_BYTES + 2) * PBL_DISPLAY_HEIGHT)];
static bool s_updating;
static UpdateCompleteCallback s_uccb;

// watch rotation
static bool s_rotated_180 = false;

static uint16_t prv_baud_prescaler(void) {
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);

  // SPI2/3 are on APB1. Pick the smallest divider not above the target.
  static const uint16_t prescalers[] = {
    SPI_BaudRatePrescaler_2,   SPI_BaudRatePrescaler_4,   SPI_BaudRatePrescaler_8,
    SPI_BaudRatePrescaler_16,  SPI_BaudRatePrescaler_32,  SPI_BaudRatePrescaler_64,
    SPI_BaudRatePrescaler_128, SPI_BaudRatePrescaler_256,
  };
  for (unsigned i = 0; i < sizeof(prescalers) / sizeof(prescalers[0]); i++) {
    if ((clocks.PCLK1_Frequency >> (i + 1)) <= DISP_SPI_CLOCK_HZ) {
      return prescalers[i];
    }
  }
  return SPI_BaudRatePrescaler_256;
}

static void prv_spi_write(const uint8_t *buf, size_t len) {
  SPI_TypeDef *spi = BOARD_CONFIG_DISPLAY.spi;

  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.cs, true);

  for (size_t i = 0; i < len; i++) {
    while (!(spi->SR & SPI_SR_TXE)) {
    }
    spi->DR = buf[i];
  }
  while (!(spi->SR & SPI_SR_TXE)) {
  }
  while (spi->SR & SPI_SR_BSY) {
  }

  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.cs, false);
}

void display_init(void) {
  RCC_APB1PeriphClockCmd(BOARD_CONFIG_DISPLAY.spi_rcc, ENABLE);

  pbl_gpio_stm32_af(&BOARD_CONFIG_DISPLAY.clk, BOARD_CONFIG_DISPLAY.spi_af, 0, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&BOARD_CONFIG_DISPLAY.mosi, BOARD_CONFIG_DISPLAY.spi_af, 0, GPIO_Speed_50MHz);
  pbl_gpio_configure(&BOARD_CONFIG_DISPLAY.cs, PBL_GPIO_OUTPUT_INACTIVE);
  pbl_gpio_configure(&BOARD_CONFIG_DISPLAY.on_ctrl, PBL_GPIO_OUTPUT_ACTIVE);

  SPI_InitTypeDef spi_cfg;
  SPI_I2S_DeInit(BOARD_CONFIG_DISPLAY.spi);
  SPI_StructInit(&spi_cfg);
  spi_cfg.SPI_Direction = SPI_Direction_1Line_Tx;
  spi_cfg.SPI_Mode = SPI_Mode_Master;
  spi_cfg.SPI_DataSize = SPI_DataSize_8b;
  spi_cfg.SPI_CPOL = SPI_CPOL_Low;
  spi_cfg.SPI_CPHA = SPI_CPHA_1Edge;
  spi_cfg.SPI_NSS = SPI_NSS_Soft;
  spi_cfg.SPI_BaudRatePrescaler = prv_baud_prescaler();
  spi_cfg.SPI_FirstBit = SPI_FirstBit_LSB;
  SPI_Init(BOARD_CONFIG_DISPLAY.spi, &spi_cfg);
  SPI_Cmd(BOARD_CONFIG_DISPLAY.spi, ENABLE);
}

void display_clear(void) {
  const uint8_t buf[] = {DISP_MODE_CLEAR, 0x00};

  PBL_ASSERTN(!s_updating);

  prv_spi_write(buf, sizeof(buf));
}

void display_set_enabled(bool enabled) {
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.on_ctrl, enabled);
}

void display_set_rotated(bool rotated) {
  s_rotated_180 = rotated;
}

static void prv_terminate_transfer(void *data) {
  s_updating = false;
  s_uccb(true);
}

void display_update(NextRowCallback nrcb, UpdateCompleteCallback uccb) {
  DisplayRow row;
  uint8_t *pbuf = s_buf;

  PBL_ASSERTN(!s_updating);

  // write command
  *pbuf++ = DISP_MODE_WRITE;

  while (nrcb(&row)) {
    // row address, data and trailing dummy
    *pbuf++ = s_rotated_180 ? (PBL_DISPLAY_HEIGHT - 1) - row.address + 1 : row.address + 1;
    if (s_rotated_180) {
      for (int i = DISP_LINE_BYTES - 1; i >= 0; --i) {
        *pbuf++ = pbl_bitrev8(row.data[i]);
      }
    } else {
      memcpy(pbuf, row.data, DISP_LINE_BYTES);
      pbuf += DISP_LINE_BYTES;
    }
    *pbuf++ = 0x00;
  }

  // last trailing dummy
  *pbuf++ = 0x00;

  s_uccb = uccb;
  s_updating = true;

  prv_spi_write(s_buf, pbuf - s_buf);

  PebbleEvent e = {
    .type = PEBBLE_CALLBACK_EVENT,
    .callback = {
      .callback = prv_terminate_transfer,
    },
  };
  event_put(&e);
}

bool display_update_in_progress(void) {
  return s_updating;
}

void display_update_boot_frame(uint8_t *framebuffer) {
}

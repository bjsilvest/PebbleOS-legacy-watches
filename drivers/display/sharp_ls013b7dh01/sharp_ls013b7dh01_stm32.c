/* SPDX-FileCopyrightText: 2025 Core Devices LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Sharp LS013B7DH01 on an STM32F4 SPI, write-only and LSB first.
//!
//! On silk the panel's VCOM (EXTCOMIN) is driven by the PMIC; on the original
//! Pebble the MCU pulses it every second.
//! Transfers are polled: an update completes before it returns.

#include <stdint.h>
#include <string.h>

#include <pbl/drivers/display/sharp_ls013b7dh01_stm32.h>
#include <pbl/drivers/gpio/stm32.h>
#include <pbl/services/regular_timer.h>
#include <pbl/util/bitops.h>

#include <board/board.h>
#include <pbl/kernel/delay.h>
#include <stm32f4xx.h>

#define DISP_MODE_WRITE 0x01U
#define DISP_MODE_CLEAR 0x04U

#define DISP_SPI_CLOCK_HZ 2000000

#define DISP_LINE_BYTES (PBL_DISPLAY_WIDTH / 8)

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

static void prv_spi_put(const uint8_t *buf, size_t len) {
  SPI_TypeDef *spi = BOARD_CONFIG_DISPLAY.spi;

  for (size_t i = 0; i < len; i++) {
    while (!(spi->SR & SPI_SR_TXE)) {
    }
    spi->DR = buf[i];
  }
}

static void prv_spi_end(void) {
  SPI_TypeDef *spi = BOARD_CONFIG_DISPLAY.spi;

  while (!(spi->SR & SPI_SR_TXE)) {
  }
  while (spi->SR & SPI_SR_BSY) {
  }

  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.cs, false);
}

static void prv_spi_write(const uint8_t *buf, size_t len) {
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.cs, true);
  prv_spi_put(buf, len);
  prv_spi_end();
}

// The panel needs its VCOM inverted regularly, or a DC bias builds up.
static void prv_pulse_vcom(void *data) {
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.extcomin, true);
  // At least 1 us high
  pbl_busy_wait_us(2);
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.extcomin, false);
}

static int prv_init(const struct pbl_display *disp) {
  RCC_APB1PeriphClockCmd(BOARD_CONFIG_DISPLAY.spi_rcc, ENABLE);

  pbl_gpio_stm32_af(&BOARD_CONFIG_DISPLAY.clk, BOARD_CONFIG_DISPLAY.spi_af, 0, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&BOARD_CONFIG_DISPLAY.mosi, BOARD_CONFIG_DISPLAY.spi_af, 0, GPIO_Speed_50MHz);
  pbl_gpio_configure(&BOARD_CONFIG_DISPLAY.cs, PBL_GPIO_OUTPUT_INACTIVE);
  if (pbl_gpio_is_connected(&BOARD_CONFIG_DISPLAY.power_5v)) {
    pbl_gpio_configure(&BOARD_CONFIG_DISPLAY.power_5v, PBL_GPIO_OUTPUT_ACTIVE);
  }
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

  if (pbl_gpio_is_connected(&BOARD_CONFIG_DISPLAY.extcomin)) {
    pbl_gpio_configure(&BOARD_CONFIG_DISPLAY.extcomin, PBL_GPIO_OUTPUT_INACTIVE);
    static RegularTimerInfo s_vcom_timer = {.cb = prv_pulse_vcom};
    regular_timer_add_seconds_callback(&s_vcom_timer);
  }

  return 0;
}

static int prv_clear(const struct pbl_display *disp) {
  const uint8_t buf[] = {DISP_MODE_CLEAR, 0x00};

  prv_spi_write(buf, sizeof(buf));

  return 0;
}

static int prv_enable(const struct pbl_display *disp) {
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.on_ctrl, true);

  return 0;
}

static int prv_disable(const struct pbl_display *disp) {
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.on_ctrl, false);

  return 0;
}

static int prv_set_rotated(const struct pbl_display *disp, bool rotated) {
  s_rotated_180 = rotated;

  return 0;
}

static int prv_update(const struct pbl_display *disp, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1) {
  // row address, data and trailing dummy
  uint8_t line[1 + DISP_LINE_BYTES + 1];
  const uint8_t cmd = DISP_MODE_WRITE;

  // The board's mounting, then the user's (left-handed) rotation on top
  const bool rotated_180 = s_rotated_180 != BOARD_CONFIG_DISPLAY.rotated_180;

  // Rows go out as they come; a whole frame buffer would cost 3.3 KB of RAM
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.cs, true);
  prv_spi_put(&cmd, sizeof(cmd));

  for (uint16_t y = y0; y <= y1; y++) {
    const uint8_t *row = buf->data + (uint32_t)y * buf->pitch;

    line[0] = rotated_180 ? (PBL_DISPLAY_HEIGHT - 1) - y + 1 : y + 1;
    if (rotated_180) {
      for (int i = 0; i < DISP_LINE_BYTES; i++) {
        line[1 + i] = pbl_bitrev8(row[DISP_LINE_BYTES - 1 - i]);
      }
    } else {
      memcpy(&line[1], row, DISP_LINE_BYTES);
    }
    line[1 + DISP_LINE_BYTES] = 0x00;
    prv_spi_put(line, sizeof(line));
  }

  // last trailing dummy
  line[0] = 0x00;
  prv_spi_put(line, 1);
  prv_spi_end();
  pbl_display_complete(disp, 0);

  return 0;
}

const struct pbl_display_ops pbl_display_sharp_stm32_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .set_rotated = prv_set_rotated,
  .update = prv_update,
  .clear = prv_clear,
};

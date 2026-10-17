/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Snowy and spalding display: an iCE40 FPGA framebuffer on an STM32F4 SPI.
//!
//! The FPGA is programmed with its bitstream at init, then takes frames: SCS
//! low, CMD_FRAME_BEGIN, then one line per panel line: a framebuffer column on
//! snowy, a row of the circular framebuffer on spalding (shifted by the
//! factory display offsets). BUSY is high while
//! the FPGA cannot take more data. Lines go out by DMA from two line buffers:
//! when one is done the next starts, from the DMA interrupt or, if BUSY is
//! high, from BUSY's falling edge, and the line after it is prepared in the
//! buffer just freed. STOP mode is blocked during a frame. The boot frame,
//! sent before the kernel runs, is polled.
//!
//! Nothing here asserts on a misbehaving FPGA: a crash loop before the
//! firmware is marked stable ends in the stock bootloader starting PRF.

#include <stdint.h>
#include <string.h>

#include <pbl/drivers/display/display.h>
#include <pbl/drivers/dma/stm32.h>
#include <pbl/drivers/exti.h>
#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/stm32/sleep.h>

#include <board/board.h>
#include <kernel/events.h>
#include <kernel/util/delay.h>
#include <stm32f4xx.h>
#if PBL_ROUND
#include <mfg/mfg_info.h>
#endif

#define CMD_FRAME_BEGIN 0x5

#if PBL_ROUND
// The panel is scanned in rows, top to bottom.
#define DISP_LINES  PBL_DISPLAY_HEIGHT
#define DISP_PIXELS PBL_DISPLAY_WIDTH
#else
// The panel is scanned in columns: each line is one framebuffer column,
// bottom to top.
#define DISP_LINES  PBL_DISPLAY_WIDTH
#define DISP_PIXELS PBL_DISPLAY_HEIGHT
#endif

//! A full frame should take no longer than 33 ms.
#define BUSY_TIMEOUT_US 40000
//! Give up on a DMA frame that has not finished by then.
#define FRAME_TIMEOUT_MS 200U
//! Typical NVCM configuration time is 56 ms.
#define CDONE_TIMEOUT_US 100000

extern const uint8_t ice40lp_bitstream_start[];
extern const uint8_t ice40lp_bitstream_end[];

static volatile bool s_updating;
static bool s_programmed;
//! Watch worn rotated by 180 degrees: frames go out flipped both ways.
static bool s_rotated_180;
static UpdateCompleteCallback s_uccb;
#if PBL_ROUND
//! Factory measured offset of the panel, in pixels.
static GPoint s_offset;
#endif

// DMA frame state. Line n is prepared in s_lines[n & 1].
static uint8_t s_lines[2][DISP_PIXELS];
static const uint8_t *s_fb;
static volatile int s_sent;
static volatile bool s_frame_active;
static volatile bool s_waiting_busy;
static RtcTicks s_frame_start;

static void prv_busy_isr(void);

static uint16_t prv_baud_prescaler(uint32_t hz) {
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);

  // SPI6 is on APB2. Pick the smallest divider not above the target.
  static const uint16_t prescalers[] = {
    SPI_BaudRatePrescaler_2,   SPI_BaudRatePrescaler_4,   SPI_BaudRatePrescaler_8,
    SPI_BaudRatePrescaler_16,  SPI_BaudRatePrescaler_32,  SPI_BaudRatePrescaler_64,
    SPI_BaudRatePrescaler_128, SPI_BaudRatePrescaler_256,
  };
  for (unsigned i = 0; i < sizeof(prescalers) / sizeof(prescalers[0]); i++) {
    if ((clocks.PCLK2_Frequency >> (i + 1)) <= hz) {
      return prescalers[i];
    }
  }
  return SPI_BaudRatePrescaler_256;
}

static void prv_spi_configure(uint32_t hz) {
  SPI_TypeDef *spi = BOARD_CONFIG_DISPLAY.spi;
  SPI_InitTypeDef spi_cfg;

  SPI_Cmd(spi, DISABLE);
  SPI_StructInit(&spi_cfg);
  spi_cfg.SPI_Direction = SPI_Direction_1Line_Tx;
  spi_cfg.SPI_Mode = SPI_Mode_Master;
  spi_cfg.SPI_DataSize = SPI_DataSize_8b;
  spi_cfg.SPI_CPOL = SPI_CPOL_High;
  spi_cfg.SPI_CPHA = SPI_CPHA_2Edge;
  spi_cfg.SPI_NSS = SPI_NSS_Soft;
  spi_cfg.SPI_BaudRatePrescaler = prv_baud_prescaler(hz);
  spi_cfg.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_Init(spi, &spi_cfg);
  SPI_Cmd(spi, ENABLE);
}

static void prv_spi_write(const uint8_t *buf, size_t len) {
  SPI_TypeDef *spi = BOARD_CONFIG_DISPLAY.spi;

  for (size_t i = 0; i < len; i++) {
    while (!(spi->SR & SPI_SR_TXE)) {
    }
    spi->DR = buf[i];
  }
  while (!(spi->SR & SPI_SR_TXE)) {
  }
  while (spi->SR & SPI_SR_BSY) {
  }
}

static bool prv_busy(void) {
  return pbl_gpio_get(&BOARD_CONFIG_DISPLAY.busy) > 0;
}

//! @return false if BUSY did not clear in time.
static bool prv_wait_not_busy(void) {
  for (int us = 0; us < BUSY_TIMEOUT_US; us += 10) {
    if (!prv_busy()) {
      return true;
    }
    delay_us(10);
  }
  return false;
}

static bool prv_program(void) {
  const struct pbl_gpio *creset = &BOARD_CONFIG_DISPLAY.creset;
  const struct pbl_gpio *scs = &BOARD_CONFIG_DISPLAY.scs;

  prv_spi_configure(BOARD_CONFIG_DISPLAY.base_spi_frequency);

  // SCS low while CRESET rises selects SPI slave configuration.
  pbl_gpio_set(scs, true);
  pbl_gpio_set(creset, false);
  // CRESET must stay low for 200 ns.
  delay_us(10);
  pbl_gpio_set(creset, true);
  // The iCE40 needs 800 us of housekeeping after reset.
  delay_us(1000);

  prv_spi_write(ice40lp_bitstream_start, ice40lp_bitstream_end - ice40lp_bitstream_start);

  // SCS high so these clocks are not taken as commands; 49+ clocks finish
  // the configuration.
  pbl_gpio_set(scs, false);
  static const uint8_t zeros[9];
  prv_spi_write(zeros, sizeof(zeros));

  bool done = false;
  for (int us = 0; us < CDONE_TIMEOUT_US; us += 100) {
    if (pbl_gpio_get(&BOARD_CONFIG_DISPLAY.cdone) > 0) {
      done = true;
      break;
    }
    delay_us(100);
  }
  if (!done) {
    PBL_LOG_ERR("FPGA CDONE not high after programming");
  }

  prv_spi_configure(BOARD_CONFIG_DISPLAY.fast_spi_frequency);
  return done;
}

static void prv_power_enable(void) {
  // 1 ms between rails; the PMIC brings up VLCD (3.2 V) first.
  delay_us(2000);
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.rail_6v6, true);
  delay_us(2000);
  pbl_gpio_set(&BOARD_CONFIG_DISPLAY.rail_4v5, true);
}

static void prv_mco1_enable(void) {
  pbl_gpio_stm32_af(&BOARD_MCO1, GPIO_AF_MCO, 0, GPIO_Speed_2MHz);
  RCC_MCO1Config(RCC_MCO1Source_LSE, RCC_MCO1Div_1);
}

void display_init(void) {
  const BoardConfigIce40Display *cfg = &BOARD_CONFIG_DISPLAY;

  prv_mco1_enable();

  RCC_APB2PeriphClockCmd(cfg->spi_rcc, ENABLE);
  pbl_gpio_stm32_af(&cfg->sclk, cfg->spi_af, 0, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&cfg->mosi, cfg->spi_af, 0, GPIO_Speed_50MHz);
  pbl_gpio_configure(&cfg->scs, PBL_GPIO_OUTPUT_INACTIVE);
  pbl_gpio_configure(&cfg->creset, PBL_GPIO_OUTPUT_ACTIVE);
  pbl_gpio_configure(&cfg->cdone, PBL_GPIO_INPUT);
  pbl_gpio_configure(&cfg->busy, PBL_GPIO_INPUT);
  pbl_gpio_configure(&cfg->rail_6v6, PBL_GPIO_OUTPUT_INACTIVE);
  pbl_gpio_configure(&cfg->rail_4v5, PBL_GPIO_OUTPUT_INACTIVE);

  exti_configure_pin(pbl_exti_stm32_config(&cfg->busy), ExtiTrigger_Falling, prv_busy_isr);

  s_programmed = prv_program();
  prv_power_enable();

#if PBL_ROUND
  s_offset = mfg_info_get_disp_offsets();
  PBL_LOG_INFO("MFG display offsets (%d,%d)", s_offset.x, s_offset.y);
#endif
}

#if PBL_ROUND
static const GBitmapDataRowInfoInternal *prv_row_info(int y) {
  return &((const GBitmapDataRowInfoInternal *)g_gbitmap_data_row_infos)[y];
}

//! Pixel x of a circular framebuffer row; black outside the circle.
static uint8_t prv_round_pixel(const uint8_t *row, const GBitmapDataRowInfoInternal *info, int x) {
  if (s_rotated_180) {
    x = PBL_DISPLAY_WIDTH - 1 - x;
  }
  return (x >= info->min_x && x <= info->max_x) ? row[x] : 0;
}
#endif

//! One panel line: the MSBs of every pixel's color channels, then the LSBs.
static void prv_get_line(const uint8_t *fb, int index, uint8_t *line) {
#if PBL_ROUND
  int y = index - s_offset.y;
  if (y < 0 || y >= PBL_DISPLAY_HEIGHT) {
    memset(line, 0, DISP_PIXELS);
    return;
  }
  if (s_rotated_180) {
    y = PBL_DISPLAY_HEIGHT - 1 - y;
  }
  const GBitmapDataRowInfoInternal *info = prv_row_info(y);
  const uint8_t *row = fb + info->offset;
  for (int dst = 0; dst < DISP_PIXELS; dst += 2) {
    const int x = dst - s_offset.x;
    const uint8_t odd = prv_round_pixel(row, info, x);
    const uint8_t even = prv_round_pixel(row, info, x + 1);
    line[dst / 2] = (odd & 0b00101010) | ((even & 0b00101010) >> 1);
    line[dst / 2 + DISP_PIXELS / 2] = ((odd & 0b00010101) << 1) | (even & 0b00010101);
  }
#else
  for (int src = 0; src < DISP_PIXELS; src += 2) {
    uint8_t even;
    uint8_t odd;
    if (s_rotated_180) {
      const int col = PBL_DISPLAY_WIDTH - 1 - index;
      even = fb[PBL_DISPLAY_WIDTH * (src + 1) + col];
      odd = fb[PBL_DISPLAY_WIDTH * src + col];
    } else {
      even = fb[PBL_DISPLAY_WIDTH * (PBL_DISPLAY_HEIGHT - 2 - src) + index];
      odd = fb[PBL_DISPLAY_WIDTH * (PBL_DISPLAY_HEIGHT - 1 - src) + index];
    }
    line[src / 2] = (odd & 0b00101010) | ((even & 0b00101010) >> 1);
    line[src / 2 + DISP_PIXELS / 2] = ((odd & 0b00010101) << 1) | (even & 0b00010101);
  }
#endif
}

static void prv_send_frame(const uint8_t *fb) {
  const struct pbl_gpio *scs = &BOARD_CONFIG_DISPLAY.scs;

  if (!prv_wait_not_busy()) {
    // A new frame (SCS falling edge) resets the FPGA's frame logic.
    PBL_LOG_WRN("Display busy before frame");
  }

  pbl_gpio_set(scs, true);
  delay_us(5);
  const uint8_t cmd = CMD_FRAME_BEGIN;
  prv_spi_write(&cmd, 1);

  // Before the kernel runs, so no DMA frame uses the line buffers.
  uint8_t *line = s_lines[0];
  for (int i = 0; i < DISP_LINES; i++) {
    prv_get_line(fb, i, line);
    if (!prv_wait_not_busy()) {
      PBL_LOG_WRN("Display stuck busy at line %d", i);
      break;
    }
    prv_spi_write(line, DISP_PIXELS);
  }

  pbl_gpio_set(scs, false);
}

static void prv_terminate_transfer(void *data);

static void prv_post_terminate(void) {
  PebbleEvent e = {
    .type = PEBBLE_CALLBACK_EVENT,
    .callback = {
      .callback = prv_terminate_transfer,
    },
  };
  event_put_isr(&e);
}

//! With interrupts locked or from an interrupt.
static void prv_end_frame(void) {
  const BoardConfigIce40Display *cfg = &BOARD_CONFIG_DISPLAY;

  exti_disable(pbl_exti_stm32_config(&cfg->busy));
  s_waiting_busy = false;
  // The last byte may still be shifting out.
  while (!(cfg->spi->SR & SPI_SR_TXE)) {
  }
  while (cfg->spi->SR & SPI_SR_BSY) {
  }
  SPI_I2S_DMACmd(cfg->spi, SPI_I2S_DMAReq_Tx, DISABLE);
  pbl_gpio_set(&cfg->scs, false);
  s_frame_active = false;
  soc_stm32_stop_release();
}

static void prv_dma_done(void *context, bool is_complete);

//! Start line s_sent and prepare the one after it.
static void prv_send_next_line(void) {
  const int line = s_sent;
  s_sent = line + 1;
  pbl_dma_stm32_start_tx(BOARD_CONFIG_DISPLAY.tx_dma, &BOARD_CONFIG_DISPLAY.spi->DR,
                         s_lines[line & 1], DISP_PIXELS, DMA_Priority_VeryHigh, prv_dma_done,
                         nullptr);
  if (s_sent < DISP_LINES) {
    prv_get_line(s_fb, s_sent, s_lines[s_sent & 1]);
  }
}

static void prv_busy_isr(void) {
  if (!s_waiting_busy) {
    return;
  }
  s_waiting_busy = false;
  exti_disable(pbl_exti_stm32_config(&BOARD_CONFIG_DISPLAY.busy));
  prv_send_next_line();
}

static void prv_dma_done(void *context, bool is_complete) {
  if (!s_frame_active) {
    return;
  }
  if (s_sent >= DISP_LINES) {
    prv_end_frame();
    prv_post_terminate();
    return;
  }
  if (prv_busy()) {
    // Arm before looking again, so the falling edge cannot be missed.
    s_waiting_busy = true;
    exti_enable(pbl_exti_stm32_config(&BOARD_CONFIG_DISPLAY.busy));
    if (prv_busy()) {
      return;
    }
    s_waiting_busy = false;
    exti_disable(pbl_exti_stm32_config(&BOARD_CONFIG_DISPLAY.busy));
  }
  prv_send_next_line();
}

//! In KernelMain.
static void prv_start_dma_frame(const uint8_t *fb) {
  const BoardConfigIce40Display *cfg = &BOARD_CONFIG_DISPLAY;

  if (!prv_wait_not_busy()) {
    // A new frame (SCS falling edge) resets the FPGA's frame logic.
    PBL_LOG_WRN("Display busy before frame");
  }

  s_fb = fb;
  s_sent = 0;
  prv_get_line(fb, 0, s_lines[0]);

  soc_stm32_stop_block();
  pbl_gpio_set(&cfg->scs, true);
  delay_us(5);
  const uint8_t cmd = CMD_FRAME_BEGIN;
  prv_spi_write(&cmd, 1);
  SPI_I2S_DMACmd(cfg->spi, SPI_I2S_DMAReq_Tx, ENABLE);

  s_frame_start = rtc_get_ticks();
  pbl_irq_lock();
  s_frame_active = true;
  prv_send_next_line();
  pbl_irq_unlock();
}

//! Time for the panel to scan out a frame before its rails drop.
#define CLEAR_SETTLE_MS 100

void display_clear(void) {
  if (!s_programmed) {
    return;
  }

  // Let a frame in flight finish; its line buffers are in use.
  for (int ms = 0; display_update_in_progress() && ms < (int)FRAME_TIMEOUT_MS; ms++) {
    delay_us(1000);
  }

  // Black is all zeros: send a frame of blank lines.
  uint8_t *line = s_lines[0];
  memset(line, 0, DISP_PIXELS);
  const struct pbl_gpio *scs = &BOARD_CONFIG_DISPLAY.scs;
  pbl_gpio_set(scs, true);
  delay_us(5);
  const uint8_t cmd = CMD_FRAME_BEGIN;
  prv_spi_write(&cmd, 1);
  for (int i = 0; i < DISP_LINES; i++) {
    if (!prv_wait_not_busy()) {
      break;
    }
    prv_spi_write(line, DISP_PIXELS);
  }
  pbl_gpio_set(scs, false);

  for (int ms = 0; ms < CLEAR_SETTLE_MS; ms++) {
    delay_us(1000);
  }
}

void display_set_enabled(bool enabled) {
  if (!s_programmed) {
    return;
  }
  if (enabled) {
    prv_power_enable();
  } else {
    // Reverse of the power-up order.
    pbl_gpio_set(&BOARD_CONFIG_DISPLAY.rail_4v5, false);
    delay_us(2000);
    pbl_gpio_set(&BOARD_CONFIG_DISPLAY.rail_6v6, false);
  }
}

void display_set_rotated(bool rotated) {
  s_rotated_180 = rotated;
}

static void prv_terminate_transfer(void *data) {
  if (!s_updating) {
    return;
  }
  s_updating = false;
  s_uccb();
}

void display_update(NextRowCallback nrcb, UpdateCompleteCallback uccb) {
  DisplayRow row;
  const uint8_t *fb = nullptr;

  // Rows are contiguous in the compositor's framebuffer; the panel takes
  // whole frames, so only the first row is needed to find it.
  while (nrcb(&row)) {
    if (!fb) {
#if PBL_ROUND
      fb = row.data - prv_row_info(row.address)->offset;
#else
      fb = row.data - (uintptr_t)row.address * PBL_DISPLAY_WIDTH;
#endif
    }
  }

  s_uccb = uccb;
  s_updating = true;

  if (fb && s_programmed) {
    prv_start_dma_frame(fb);
    return;
  }

  PebbleEvent e = {
    .type = PEBBLE_CALLBACK_EVENT,
    .callback = {
      .callback = prv_terminate_transfer,
    },
  };
  event_put(&e);
}

bool display_update_in_progress(void) {
  if (s_updating && s_frame_active &&
      (rtc_get_ticks() - s_frame_start) > (FRAME_TIMEOUT_MS * RTC_TICKS_HZ) / 1000U) {
    // Stuck (e.g. BUSY never fell): end the frame rather than hang the UI.
    bool ended = false;
    pbl_irq_lock();
    if (s_frame_active) {
      pbl_dma_stm32_stop(BOARD_CONFIG_DISPLAY.tx_dma);
      prv_end_frame();
      ended = true;
    }
    pbl_irq_unlock();
    if (ended) {
      PBL_LOG_WRN("Display frame timed out at line %d", s_sent);
      prv_post_terminate();
    }
  }
  return s_updating;
}

void display_update_boot_frame(uint8_t *framebuffer) {
  if (s_programmed) {
    prv_send_frame(framebuffer);
  }
}

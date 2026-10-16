/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

// Original Pebble (tintin): Kickstarter ev2_4 and the bb2 bigboard QEMU
// emulates. Pins from the stock board_ev2_4.c/.h and board_bb2.c/.h; the two
// differ only in the USB power and Bluetooth wake lines (swapped), the vibe
// pin and the charger controls.

#include <pbl/drivers/backlight/pwm.h>
#include <pbl/services/imu/units.h>

#define BT_VENDOR_ID   0x0154
#define BT_VENDOR_NAME "Pebble Technology"

static const BoardConfig BOARD_CONFIG = {
#ifdef CONFIG_BOARD_TINTIN_BB2
  .ambient_light_dark_threshold = 3000,
#else
  .ambient_light_dark_threshold = 3180,
#endif
  .ambient_k_delta_threshold = 96,
  .photo_en = PBL_GPIO(STM32_GPIOD, 2, 0),
  .als_adc_channel = ADC_Channel_12,
  .als_adc_pin = PBL_GPIO(STM32_GPIOC, 2, 0),
  .backlight_on_percent = 25,
};

// Pressed buttons pull their line to the common line (PA0), which the board
// drives low; the lines have external pull-ups.
static const struct pbl_gpio BOARD_BUTTON_COM = PBL_GPIO(STM32_GPIOA, 0, 0);

static const BoardConfigButton BOARD_CONFIG_BUTTON = {
  .buttons = {
    [BUTTON_ID_BACK] = {"Back", PBL_GPIO(STM32_GPIOC, 3, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_BACK},
    [BUTTON_ID_UP] = {"Up", PBL_GPIO(STM32_GPIOA, 2, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_UP},
    [BUTTON_ID_SELECT] =
        {"Select", PBL_GPIO(STM32_GPIOC, 6, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_SELECT},
    [BUTTON_ID_DOWN] = {"Down", PBL_GPIO(STM32_GPIOA, 1, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_DOWN},
  },
};

static const BoardConfigPower BOARD_CONFIG_POWER = {
#ifdef CONFIG_BOARD_TINTIN_BB2
  .low_power_threshold = 5,
  .vusb_stat = PBL_GPIO(STM32_GPIOC, 13, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
  .chg_fast = PBL_GPIO(STM32_GPIOB, 6, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_OPEN_DRAIN),
  .chg_en = PBL_GPIO(STM32_GPIOB, 9, PBL_GPIO_OPEN_DRAIN),
#else
  .low_power_threshold = 3,
  .vusb_stat = PBL_GPIO(STM32_GPIOC, 12, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
#endif
  .battery_capacity_hours = 144,
  .chg_stat = PBL_GPIO(STM32_GPIOH, 1, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
  // 750k over 470k, with the stock driver's allowance for leakage
  .vbat_adc_channel = ADC_Channel_10,
  .vbat_adc_pin = PBL_GPIO(STM32_GPIOC, 0, 0),
  .vbat_scale_numerator = 3599,
  .vbat_scale_denominator = 1373,
};

static const BoardConfigAccel BOARD_CONFIG_ACCEL = {
  .accel_config =
      {
        .axes_offsets[AXIS_X] = 1,
        .axes_offsets[AXIS_Y] = 0,
        .axes_offsets[AXIS_Z] = 2,
        .axes_inverts[AXIS_X] = true,
        .axes_inverts[AXIS_Y] = false,
        .axes_inverts[AXIS_Z] = true,
        .shake_thresholds[AccelThresholdHigh] = 0x7f,
        .shake_thresholds[AccelThresholdLow] = 0xa,
      },
  .accel_int_gpios = {
    PBL_GPIO(STM32_GPIOC, 8, 0),
    PBL_GPIO(STM32_GPIOC, 9, 0),
  },
};

// Vibe: switched, no strength control
static const BoardConfigActuator BOARD_CONFIG_VIBE = {
#ifdef CONFIG_BOARD_TINTIN_BB2
  .ctl = PBL_GPIO(STM32_GPIOB, 13, 0),
#else
  .ctl = PBL_GPIO(STM32_GPIOB, 0, 0),
#endif
};

// Backlight: TIM3 CH2
static const BacklightPwmConfig BACKLIGHT_PWM = {
  .pwm =
      {
        .output = PBL_GPIO(STM32_GPIOB, 5, 0),
        .timer = TIM3,
        .rcc = RCC_APB1Periph_TIM3,
        .af = GPIO_AF_TIM3,
        .channel = 2,
      },
  .max_duty_cycle_percent = 100,
};

static const BoardConfigSharpDisplay BOARD_CONFIG_DISPLAY = {
  .spi = SPI2,
  .spi_rcc = RCC_APB1Periph_SPI2,
  .spi_af = GPIO_AF_SPI2,
  .clk = PBL_GPIO(STM32_GPIOB, 13, 0),
  .mosi = PBL_GPIO(STM32_GPIOB, 15, 0),
  .cs = PBL_GPIO(STM32_GPIOB, 12, 0),
  .on_ctrl = PBL_GPIO(STM32_GPIOB, 14, PBL_GPIO_OPEN_DRAIN),
  .power_5v = PBL_GPIO(STM32_GPIOC, 5, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_OPEN_DRAIN),
  .extcomin = PBL_GPIO(STM32_GPIOB, 1, 0),
  .rotated_180 = true,
};

// N25Q032 on SPI1
static const BoardConfigNorSpi BOARD_CONFIG_NOR_SPI = {
  .spi = SPI1,
  .spi_rcc = RCC_APB2Periph_SPI1,
  .spi_af = GPIO_AF_SPI1,
  .sclk = PBL_GPIO(STM32_GPIOA, 5, 0),
  .miso = PBL_GPIO(STM32_GPIOA, 6, 0),
  .mosi = PBL_GPIO(STM32_GPIOA, 7, 0),
  .scs = PBL_GPIO(STM32_GPIOA, 4, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
};

// MCO1 on PA8 outputs the LSE, the CC2564's 32 kHz slow clock.
static const struct pbl_gpio BOARD_MCO1 = PBL_GPIO(STM32_GPIOA, 8, 0);

extern UARTDevice *const DBG_UART;

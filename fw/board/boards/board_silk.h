/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/backlight/pwm.h>
#include <pbl/services/imu/units.h>

#define BT_VENDOR_ID   0x0154
#define BT_VENDOR_NAME "Pebble Technology"

static const BoardConfig BOARD_CONFIG = {
  .ambient_light_dark_threshold = 150,
  .ambient_k_delta_threshold = 50,
  .photo_en = PBL_GPIO(STM32_GPIOC, 0, 0),
  .als_always_on = true,
  .als_adc_channel = ADC_Channel_13,
  .als_adc_pin = PBL_GPIO(STM32_GPIOC, 3, 0),
  .backlight_on_percent = 25,
};

// Active high: Back has an external pull-down, the others use the internal one.
static const BoardConfigButton BOARD_CONFIG_BUTTON = {
  .buttons = {
    [BUTTON_ID_BACK] = {"Back", PBL_GPIO(STM32_GPIOC, 13, 0), PBL_INPUT_KEY_BACK},
    [BUTTON_ID_UP] = {"Up", PBL_GPIO(STM32_GPIOD, 2, PBL_GPIO_PULL_DOWN), PBL_INPUT_KEY_UP},
    [BUTTON_ID_SELECT] =
        {"Select", PBL_GPIO(STM32_GPIOH, 0, PBL_GPIO_PULL_DOWN), PBL_INPUT_KEY_SELECT},
    [BUTTON_ID_DOWN] = {"Down", PBL_GPIO(STM32_GPIOH, 1, PBL_GPIO_PULL_DOWN), PBL_INPUT_KEY_DOWN},
  },
};

static const BoardConfigPower BOARD_CONFIG_POWER = {
  .low_power_threshold = 5,
  // Stock estimate: 125 mAh / ~0.8 mA average
  .battery_capacity_hours = 155,
};

static const BoardConfigAccel BOARD_CONFIG_ACCEL = {
  .accel_config =
      {
        .axes_offsets[AXIS_X] = 0,
        .axes_offsets[AXIS_Y] = 1,
        .axes_offsets[AXIS_Z] = 2,
        .axes_inverts[AXIS_X] = true,
        .axes_inverts[AXIS_Y] = true,
        .axes_inverts[AXIS_Z] = true,
        // Tuned for the BMA255 locked at 125 Hz.
        .shake_thresholds[AccelThresholdHigh] = 64,
        .shake_thresholds[AccelThresholdLow] = 0xf,
        .double_tap_threshold = 12500,
      },
  .accel_int_gpios =
      {
        PBL_GPIO(STM32_GPIOA, 6, 0),
        PBL_GPIO(STM32_GPIOA, 3, 0),
      },
  .default_motion_sensitivity = 55U, // Medium
};

// Vibe: DRV2603 driven by TIM14 CH1
static const BoardConfigActuator BOARD_CONFIG_VIBE = {
  .pwm =
      {
        .output = PBL_GPIO(STM32_GPIOA, 7, 0),
        .timer = TIM14,
        .rcc = RCC_APB1Periph_TIM14,
        .af = GPIO_AF_TIM14,
        .channel = 1,
      },
  .vsys_scale = 3300,
};

// Backlight: TIM3 CH1, enabled by PB13
static const BacklightPwmConfig BACKLIGHT_PWM = {
  .ctl = PBL_GPIO(STM32_GPIOB, 13, 0),
  .pwm =
      {
        .output = PBL_GPIO(STM32_GPIOC, 6, 0),
        .timer = TIM3,
        .rcc = RCC_APB1Periph_TIM3,
        .af = GPIO_AF_TIM3,
        .channel = 1,
      },
  .max_duty_cycle_percent = 67,
};

static const BoardConfigSharpDisplay BOARD_CONFIG_DISPLAY = {
  .spi = SPI2,
  .spi_rcc = RCC_APB1Periph_SPI2,
  .spi_af = GPIO_AF_SPI2,
  .clk = PBL_GPIO(STM32_GPIOB, 10, 0),
  .mosi = PBL_GPIO(STM32_GPIOB, 15, 0),
  .cs = PBL_GPIO(STM32_GPIOB, 9, 0),
  .on_ctrl = PBL_GPIO(STM32_GPIOA, 0, 0),
};

extern UARTDevice *const DBG_UART;

#ifdef CONFIG_BT_HCI_UART
// DA14681 HCI: the bootrom UART pins, RX on USART6 and TX on USART2.
extern UARTDevice *const BT_HCI_UART;
extern UARTDevice *const BT_HCI_TX_UART;
#define BT_HCI_UART_TX BT_HCI_TX_UART

// DA14681 reset, active high.
static const struct pbl_gpio BOARD_BT_RESET = PBL_GPIO(STM32_GPIOC, 5, 0);
#endif
extern QSPIPort *const QSPI;
extern QSPIFlash *const QSPI_FLASH;
extern MicDevice *const MIC;

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#define BT_VENDOR_ID   0x0154
#define BT_VENDOR_NAME "Pebble Technology"

static const BoardConfig BOARD_CONFIG = {
  .ambient_light_dark_threshold = 150,
  .ambient_k_delta_threshold = 50,
  .photo_en = PBL_GPIO(STM32_GPIOC, 0, 0),
  .als_always_on = true,
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
  .default_motion_sensitivity = 55U, // Medium
};

extern UARTDevice *const DBG_UART;
extern QSPIPort *const QSPI;
extern QSPIFlash *const QSPI_FLASH;

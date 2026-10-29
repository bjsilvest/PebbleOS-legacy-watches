/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/regulator/gpio.h>
#ifdef CONFIG_HRM
#include <pbl/drivers/hrm/as7000.h>
#endif
#include <pbl/services/imu/units.h>

#define BT_VENDOR_ID   0x0154
#define BT_VENDOR_NAME "Pebble Technology"

static const BoardConfig BOARD_CONFIG = {
  // In lux: the stock raw threshold (150) through the provisional table in
  // board_silk.c, so the backlight still switches where it did.
  .ambient_light_dark_threshold = 347,
  .ambient_k_delta_threshold = 100,
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

// H4 flow control over spare lines of stock's SPI link, routed there by the
// controller image: its UART RTS (radio P0_0, low when it can take bytes) and
// CTS (radio P0_1, held low so it may always send). The bootrom probes these
// for SPI, so CTS is driven only once the controller image runs.
#define BOARD_BT_HAS_FLOW_CONTROL 1
static const struct pbl_gpio BOARD_BT_RTS =
    PBL_GPIO(STM32_GPIOB, 0, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP);
static const struct pbl_gpio BOARD_BT_CTS = PBL_GPIO(STM32_GPIOB, 1, PBL_GPIO_ACTIVE_LOW);

// Radio P0_4 (stock's SPI data in): the controller image drives it high while
// it has data for the host, whose CTS may be deasserted for STOP mode, so it
// wakes the host.
#define BOARD_BT_HAS_WAKE 1
static const struct pbl_gpio BOARD_BT_WAKE = PBL_GPIO(STM32_GPIOA, 10, PBL_GPIO_PULL_DOWN);
#endif
#ifdef CONFIG_HRM
extern HRMDevice *const HRM;
#endif
extern QSPIPort *const QSPI;
extern QSPIFlash *const QSPI_FLASH;
extern MicDevice *const MIC;

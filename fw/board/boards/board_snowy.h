/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

// Pebble Time (snowy_dvt), Pebble Time Steel (snowy_s3) and Pebble Time Round
// (spalding), electrically the same but for the round display and the few
// values below. Pins from the stock board_snowy.c/.h and board_spalding_evt.c/.h.

#include <pbl/services/imu/units.h>

#define BT_VENDOR_ID   0x0154
#define BT_VENDOR_NAME "Pebble Technology"

static const BoardConfig BOARD_CONFIG = {
  // As the lux-calibrated Core watches: the backlight stops coming on in
  // light brighter than a desk lamp.
  .ambient_light_dark_threshold = 800,
  .ambient_k_delta_threshold = 100,
#ifdef CONFIG_BOARD_SPALDING
  .backlight_on_percent = 25,
#else
  .backlight_on_percent = 45,
#endif
};

// Active low with pull-ups; Back has an external one.
static const BoardConfigButton BOARD_CONFIG_BUTTON = {
  .buttons = {
    [BUTTON_ID_BACK] = {"Back", PBL_GPIO(STM32_GPIOG, 4, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_BACK},
    [BUTTON_ID_UP] =
        {"Up", PBL_GPIO(STM32_GPIOG, 3, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP), PBL_INPUT_KEY_UP},
    [BUTTON_ID_SELECT] =
        {"Select", PBL_GPIO(STM32_GPIOG, 1, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
         PBL_INPUT_KEY_SELECT},
    [BUTTON_ID_DOWN] = {
      "Down", PBL_GPIO(STM32_GPIOG, 2, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP), PBL_INPUT_KEY_DOWN
    },
  },
};

static const BoardConfigPower BOARD_CONFIG_POWER = {
#ifdef CONFIG_BOARD_SNOWY_S3
  .low_power_threshold = 2,
  .battery_capacity_hours = 204,
#elifdef CONFIG_BOARD_SPALDING
  .low_power_threshold = 7,
  .battery_capacity_hours = 44,
#else
  .low_power_threshold = 3,
  .battery_capacity_hours = 144,
#endif
};

static const BoardConfigAccel BOARD_CONFIG_ACCEL = {
  .accel_config =
      {
#ifdef CONFIG_BOARD_SPALDING
        .axes_offsets[AXIS_X] = 0,
        .axes_offsets[AXIS_Y] = 1,
        .axes_offsets[AXIS_Z] = 2,
        .axes_inverts[AXIS_X] = false,
        .axes_inverts[AXIS_Y] = true,
        .axes_inverts[AXIS_Z] = false,
#else
        .axes_offsets[AXIS_X] = 1,
        .axes_offsets[AXIS_Y] = 0,
        .axes_offsets[AXIS_Z] = 2,
        .axes_inverts[AXIS_X] = false,
        .axes_inverts[AXIS_Y] = false,
        .axes_inverts[AXIS_Z] = false,
#endif
        .shake_thresholds[AccelThresholdHigh] = 0x64,
        .shake_thresholds[AccelThresholdLow] = 0xf,
        .double_tap_threshold = 12500,
      },
  .accel_int_gpios = {
    PBL_GPIO(STM32_GPIOG, 5, 0),
    PBL_GPIO(STM32_GPIOG, 6, 0),
  },
};

extern const struct pbl_dma_stm32_stream ICE40LP_TX_DMA;

static const BoardConfigIce40Display BOARD_CONFIG_DISPLAY = {
  .spi = SPI6,
  .spi_rcc = RCC_APB2Periph_SPI6,
  .spi_af = GPIO_AF_SPI6,
  .tx_dma = &ICE40LP_TX_DMA,
  .sclk = PBL_GPIO(STM32_GPIOG, 13, 0),
  .mosi = PBL_GPIO(STM32_GPIOG, 14, 0),
  .scs = PBL_GPIO(STM32_GPIOG, 8, PBL_GPIO_ACTIVE_LOW),
  .creset = PBL_GPIO(STM32_GPIOG, 15, PBL_GPIO_OPEN_DRAIN),
  .cdone = PBL_GPIO(STM32_GPIOG, 9, 0),
  .busy = PBL_GPIO(STM32_GPIOG, 10, 0),
#ifdef CONFIG_BOARD_SPALDING
  // Push-pull on spalding, as the stock firmware drove it.
  .rail_6v6 = PBL_GPIO(STM32_GPIOF, 3, 0),
#else
  .rail_6v6 = PBL_GPIO(STM32_GPIOF, 3, PBL_GPIO_OPEN_DRAIN),
#endif
  .rail_4v5 = PBL_GPIO(STM32_GPIOF, 2, PBL_GPIO_OPEN_DRAIN),
  .base_spi_frequency = 16000000,
  .fast_spi_frequency = 32000000,
};

// MCO1 on PA8 outputs the LSE, the 32 kHz clock of the FPGA and the CC2564.
static const struct pbl_gpio BOARD_MCO1 = PBL_GPIO(STM32_GPIOA, 8, 0);

extern UARTDevice *const DBG_UART;

#ifdef CONFIG_BT_HCI_UART
// CC2564B HCI: USART1 with hardware flow control.
extern UARTDevice *const BT_HCI_UART;
#define BOARD_BT_HCI_USART USART1

// nSHUTDOWN: active (low) keeps the controller off.
static const struct pbl_gpio BOARD_BT_SHUTDOWN = PBL_GPIO(STM32_GPIOB, 12, PBL_GPIO_ACTIVE_LOW);
// The controller's RTS, our CTS: active (low) when it can take bytes.
static const struct pbl_gpio BOARD_BT_CTS = PBL_GPIO(STM32_GPIOA, 11, PBL_GPIO_ACTIVE_LOW);
// Our RTS, normally driven by the UART; held high while the controller sleeps.
static const struct pbl_gpio BOARD_BT_HOST_RTS = PBL_GPIO(STM32_GPIOA, 12, 0);
#endif
extern MicDevice *const MIC;

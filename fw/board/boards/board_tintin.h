/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

// Original Pebble (tintin): Kickstarter ev2_4, retail v1_5, the Pebble Steel
// (v2_0) and the bb2 bigboard QEMU emulates. Pins from the stock
// board_ev2_4.c/.h, board_v1_5.c/.h, board_v2_0.c/.h and board_bb2.c/.h.
// ev2_4 has the USB power and Bluetooth wake lines swapped and no charger
// controls; bb2 has the vibe on another pin; v1_5 and v2_0 have the debug UART
// on other pins; v2_0 has its backlight on an I2C LED controller.

#ifdef CONFIG_BACKLIGHT_PWM
#include <pbl/drivers/backlight/pwm.h>
#endif
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/regulator/gpio.h>
#include <pbl/services/imu/units.h>

#define BT_VENDOR_ID   0x0154
#define BT_VENDOR_NAME "Pebble Technology"

static const BoardConfig BOARD_CONFIG = {
#if defined(CONFIG_BOARD_TINTIN_BB2)
  .ambient_light_dark_threshold = 3000,
#elif defined(CONFIG_BOARD_TINTIN_V1_5)
  .ambient_light_dark_threshold = 3120,
#elif defined(CONFIG_BOARD_TINTIN_V2_0)
  .ambient_light_dark_threshold = 3200,
#else
  .ambient_light_dark_threshold = 3180,
#endif
  .ambient_k_delta_threshold = 96,
  .photo_en = PBL_GPIO(STM32_GPIOD, 2, 0),
  .als_adc_channel = ADC_Channel_12,
  .als_adc_pin = PBL_GPIO(STM32_GPIOC, 2, 0),
#ifdef CONFIG_BOARD_TINTIN_V2_0
  .backlight_on_percent = 100,
#else
  .backlight_on_percent = 25,
#endif
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
#if defined(CONFIG_BOARD_TINTIN_BB2)
  .low_power_threshold = 5,
#elif defined(CONFIG_BOARD_TINTIN_V2_0)
  .low_power_threshold = 6,
#else
  .low_power_threshold = 3,
#endif
#ifndef CONFIG_BOARD_TINTIN_EV2_4
  .vusb_stat = PBL_GPIO(STM32_GPIOC, 13, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_PULL_UP),
  .chg_fast = PBL_GPIO(STM32_GPIOB, 6, PBL_GPIO_ACTIVE_LOW | PBL_GPIO_OPEN_DRAIN),
  .chg_en = PBL_GPIO(STM32_GPIOB, 9, PBL_GPIO_OPEN_DRAIN),
#else
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

#ifdef CONFIG_BACKLIGHT_IS31FL3196
// Backlight and status LED: IS31FL3196 on I2C1, shutdown line on PB5
// (active = running)
extern const struct pbl_i2c_dev *const I2C_IS31FL3196;
static const struct pbl_gpio BOARD_BACKLIGHT_SDB = PBL_GPIO(STM32_GPIOB, 5, 0);
// Battery voltage drop while the status LED is lit
#define BOARD_STATUS_LED_COMPENSATION_MV 30
#else
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
#endif

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

// LIS3DH on I2C1 (PB8 SCL, PB7 SDA)
extern const struct pbl_i2c_dev *const I2C_LIS3DH;

// MAG3110 on I2C2 (PB10 SCL, PB11 SDA), whose 2.5 V rail (PH0) also feeds
// the unused Apple MFi chip; on only while the magnetometer is in use.
static const BoardConfigMag BOARD_CONFIG_MAG = {
  .mag_config = {
    .axes_offsets[AXIS_X] = 1,
    .axes_offsets[AXIS_Y] = 0,
    .axes_offsets[AXIS_Z] = 2,
    .axes_inverts[AXIS_X] = false,
    .axes_inverts[AXIS_Y] = true,
    .axes_inverts[AXIS_Z] = true,
  },
};
// Data ready, active high
static const struct pbl_gpio BOARD_MAG_INT = PBL_GPIO(STM32_GPIOC, 4, 0);
extern const struct pbl_i2c_dev *const I2C_MAG3110;
extern const struct pbl_gpio_regulator BOARD_RAIL_2V5;
#define BOARD_MAG_RAIL (&BOARD_RAIL_2V5.reg)

#ifdef CONFIG_BT_HCI_UART
// CC2564A HCI: USART1 with hardware flow control.
extern UARTDevice *const BT_HCI_UART;
#define BOARD_BT_HCI_USART USART1

// nSHUTDOWN: active (low) keeps the controller off.
static const struct pbl_gpio BOARD_BT_SHUTDOWN = PBL_GPIO(STM32_GPIOA, 3, PBL_GPIO_ACTIVE_LOW);
// The controller's RTS, our CTS: active (low) when it can take bytes.
static const struct pbl_gpio BOARD_BT_CTS = PBL_GPIO(STM32_GPIOA, 11, PBL_GPIO_ACTIVE_LOW);
// Our RTS, normally driven by the UART; held high while the controller sleeps.
static const struct pbl_gpio BOARD_BT_HOST_RTS = PBL_GPIO(STM32_GPIOA, 12, 0);
#endif

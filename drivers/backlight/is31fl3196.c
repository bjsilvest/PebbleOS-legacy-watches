/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

// ISSI IS31FL3196 six channel LED driver of the Pebble Steel: OUT1-3 drive the
// backlight. Register use as in the stock led_controller.c. The chip runs only
// while BOARD_BACKLIGHT_SDB is active (its SDB pin high), which brings the
// shutdown current down when the backlight is off.

#include <pbl/drivers/backlight.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/logging/logging.h>

#include <board/board.h>

PBL_LOG_MODULE_DEFINE(driver_backlight_is31fl3196, CONFIG_DRIVER_BACKLIGHT_LOG_LEVEL);

#define IS31FL3196_REG_SHUTDOWN        0x00U
#define IS31FL3196_REG_SHUTDOWN_NORMAL 0x01U

// PWM mode, audio off, AGC on (fast)
#define IS31FL3196_REG_CONFIG1 0x03U
// Master control, 25 mA, 0 dB gain
#define IS31FL3196_REG_CONFIG2       0x04U
#define IS31FL3196_REG_CONFIG2_25MA  0x70U
#define IS31FL3196_REG_RAMPING_MODE  0x05U
#define IS31FL3196_REG_BREATHING     0x06U
#define IS31FL3196_REG_PWM_OUT1      0x07U
#define IS31FL3196_REG_DATA_UPDATE   0x10U
#define IS31FL3196_REG_RESET         0xFFU
// Any value latches the PWM registers or resets the chip
#define IS31FL3196_LATCH 0xAAU

#define IS31FL3196_BACKLIGHT_OUTPUTS 3U

static bool s_initialized;
static uint8_t s_brightness;

static bool prv_write_register(uint8_t reg, uint8_t value) {
  return pbl_i2c_write_register_block(I2C_IS31FL3196, reg, 1, &value) == 0;
}

static bool prv_write_pwm(uint8_t pwm) {
  bool ret = true;
  for (uint8_t i = 0; i < IS31FL3196_BACKLIGHT_OUTPUTS; i++) {
    ret &= prv_write_register(IS31FL3196_REG_PWM_OUT1 + i, pwm);
  }
  return ret && prv_write_register(IS31FL3196_REG_DATA_UPDATE, IS31FL3196_LATCH);
}

void backlight_init(void) {
  pbl_gpio_configure(&BOARD_BACKLIGHT_SDB, PBL_GPIO_OUTPUT_INACTIVE);
  pbl_gpio_set(&BOARD_BACKLIGHT_SDB, true);

  pbl_i2c_use(I2C_IS31FL3196);
  // A missing controller must not be fatal: a dark backlight beats a boot
  // loop.
  if (!prv_write_register(IS31FL3196_REG_RESET, IS31FL3196_LATCH)) {
    PBL_LOG_ERR("IS31FL3196 not detected; backlight disabled");
  } else {
    s_initialized = prv_write_register(IS31FL3196_REG_SHUTDOWN, IS31FL3196_REG_SHUTDOWN_NORMAL) &&
                    prv_write_register(IS31FL3196_REG_CONFIG1, 0) &&
                    prv_write_register(IS31FL3196_REG_CONFIG2, IS31FL3196_REG_CONFIG2_25MA) &&
                    prv_write_register(IS31FL3196_REG_RAMPING_MODE, 0) &&
                    prv_write_register(IS31FL3196_REG_BREATHING, 0);
    if (!s_initialized) {
      PBL_LOG_ERR("IS31FL3196 init write failed (i2c)");
    }
  }
  pbl_i2c_release(I2C_IS31FL3196);

  pbl_gpio_set(&BOARD_BACKLIGHT_SDB, false);
}

static void prv_apply(void) {
  if (!s_initialized) {
    return;
  }

  pbl_gpio_set(&BOARD_BACKLIGHT_SDB, true);

  pbl_i2c_use(I2C_IS31FL3196);
  if (!prv_write_pwm((uint8_t)(s_brightness * 255U / 100U))) {
    PBL_LOG_ERR("IS31FL3196 set brightness failed (i2c)");
  }
  pbl_i2c_release(I2C_IS31FL3196);

  if (s_brightness == 0U) {
    pbl_gpio_set(&BOARD_BACKLIGHT_SDB, false);
  }
}

void backlight_set_brightness(uint8_t brightness) {
  if (brightness > 100U) {
    brightness = 100U;
  }
  s_brightness = brightness;
  prv_apply();
}

uint8_t backlight_get_level(uint8_t brightness) {
  // 255-step PWM: every brightness value is distinct.
  return brightness;
}

void backlight_refresh(void) {
  if (s_brightness != 0U) {
    prv_apply();
  }
}

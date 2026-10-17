/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

// ISSI IS31FL3196 six channel LED driver of the Pebble Steel: OUT1-3 drive the
// backlight, OUT4-6 the red, green and blue status LED. Register use as in the
// stock led_controller.c. The chip runs only while BOARD_BACKLIGHT_SDB is
// active (its SDB pin high), which brings the shutdown current down when both
// are off.

#include <pbl/drivers/backlight.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/status_led.h>
#include <pbl/logging/logging.h>
#include <pbl/services/battery/battery_curve.h>

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
#define IS31FL3196_REG_PWM_OUT4      0x0AU
#define IS31FL3196_REG_DATA_UPDATE   0x10U
#define IS31FL3196_REG_RESET         0xFFU
// Any value latches the PWM registers or resets the chip
#define IS31FL3196_LATCH 0xAAU

// Status LED colors (0xRRGGBB, of 255): dim, as the LED stays lit while
// charging
#define STATUS_LED_COLOR_CHARGING       0x0F2300U
#define STATUS_LED_COLOR_FULLY_CHARGED  0x003C00U

static bool s_initialized;
static uint8_t s_brightness;
static uint32_t s_status_color;

static bool prv_write_register(uint8_t reg, uint8_t value) {
  return pbl_i2c_write_register_block(I2C_IS31FL3196, reg, 1, &value) == 0;
}

// Writes OUT1-3 or OUT4-6 from 0xRRGGBB (OUTn+0 = red) and latches them.
static void prv_write_outputs(uint8_t first_reg, uint32_t rgb) {
  if (!s_initialized) {
    return;
  }

  pbl_gpio_set(&BOARD_BACKLIGHT_SDB, true);

  pbl_i2c_use(I2C_IS31FL3196);
  bool ret = true;
  for (int i = 0; i < 3; i++) {
    ret &= prv_write_register(first_reg + i, (uint8_t)(rgb >> (16 - 8 * i)));
  }
  ret &= prv_write_register(IS31FL3196_REG_DATA_UPDATE, IS31FL3196_LATCH);
  pbl_i2c_release(I2C_IS31FL3196);
  if (!ret) {
    PBL_LOG_ERR("IS31FL3196 write failed (i2c)");
  }

  if (s_brightness == 0U && s_status_color == 0U) {
    pbl_gpio_set(&BOARD_BACKLIGHT_SDB, false);
  }
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

static void prv_apply_brightness(void) {
  const uint32_t pwm = s_brightness * 255U / 100U;
  prv_write_outputs(IS31FL3196_REG_PWM_OUT1, pwm << 16 | pwm << 8 | pwm);
}

void backlight_set_brightness(uint8_t brightness) {
  if (brightness > 100U) {
    brightness = 100U;
  }
  s_brightness = brightness;
  prv_apply_brightness();
}

uint8_t backlight_get_level(uint8_t brightness) {
  // 255-step PWM: every brightness value is distinct.
  return brightness;
}

void backlight_refresh(void) {
  if (s_brightness != 0U) {
    prv_apply_brightness();
  }
}

void status_led_set(StatusLedState state) {
  static const uint32_t s_colors[] = {
    [StatusLedState_Off] = 0,
    [StatusLedState_Charging] = STATUS_LED_COLOR_CHARGING,
    [StatusLedState_FullyCharged] = STATUS_LED_COLOR_FULLY_CHARGED,
  };

  const uint32_t color = s_colors[state];
  if (color == s_status_color) {
    return;
  }
  s_status_color = color;

  // The lit LED's load lowers the measured battery voltage.
  battery_curve_set_compensation(BATTERY_CURVE_COMPENSATE_STATUS_LED,
                                 color ? BOARD_STATUS_LED_COMPENSATION_MV : 0);

  prv_write_outputs(IS31FL3196_REG_PWM_OUT4, color);
}

/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/backlight.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>

/**
 * @defgroup drivers_backlight_is31fl3196 IS31FL3196
 * @ingroup drivers_backlight
 * @brief ISSI IS31FL3196 @ref drivers_backlight driver, which also drives the status LED.
 *
 * @code{.c}
 * PBL_IS31FL3196_DEFINE(s_is31fl3196, &s_i2c.bus, 0x64, .sdb = PBL_GPIO(STM32_GPIOB, 5, 0),
 *                       .status_led_compensation_mv = 30);
 *
 * const struct pbl_backlight *const BACKLIGHT = &s_is31fl3196.backlight;
 * @endcode
 * @{
 */

/** @brief IS31FL3196 LED controller. */
struct pbl_is31fl3196 {
  /** Backlight, child of the I2C bus. */
  struct pbl_backlight backlight;
  /** I2C device. */
  struct pbl_i2c_dev i2c;
  /** Shutdown line, active while the chip runs. */
  struct pbl_gpio sdb;
  /** Battery voltage drop while the status LED is lit, in mV. */
  uint16_t status_led_compensation_mv;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_backlight_ops pbl_is31fl3196_ops;
/** @endcond */

/**
 * @brief Define an IS31FL3196.
 *
 * @param sym Symbol of the instance; the backlight is @c sym.backlight.
 * @param _bus I2C bus.
 * @param _addr 7-bit I2C address.
 * @param ... Designated initializers of the remaining fields: @c sdb and
 *            @c status_led_compensation_mv.
 */
#define PBL_IS31FL3196_DEFINE(sym, _bus, _addr, ...)                                            \
  PBL_BACKLIGHT_STATE_DEFINE(sym);                                                              \
  const struct pbl_is31fl3196 sym = {                                                           \
    .backlight = PBL_BACKLIGHT_INIT(sym, "is31fl3196", &(_bus)->dev, &pbl_is31fl3196_ops),      \
    .i2c = PBL_I2C_DEV(_bus, _addr),                                                            \
    __VA_ARGS__                                                                                 \
  };                                                                                            \
  PBL_DEVICE_REGISTER(sym, &sym.backlight.dev)

/** @} */

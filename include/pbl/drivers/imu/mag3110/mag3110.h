/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/mag.h>

/**
 * @defgroup drivers_mag_mag3110 MAG3110
 * @ingroup drivers_mag
 * @brief NXP MAG3110 @ref drivers_mag driver.
 *
 * @code{.c}
 * PBL_MAG3110_DEFINE(s_mag3110, &s_i2c.bus, 0x0e, .int_gpio = PBL_GPIO(STM32_GPIOF, 14, 0),
 *                    .axis_map = {1, 0, 2}, .axis_dir = {-1, -1, 1});
 *
 * const struct pbl_mag *const MAG = &s_mag3110.mag;
 * @endcode
 * @{
 */

/** @cond INTERNAL_HIDDEN */
struct pbl_mag3110_state {
  uint8_t rate_hz;
  bool rotated;
};
/** @endcond */

/** @brief MAG3110 magnetometer. */
struct pbl_mag3110 {
  /** Magnetometer, child of the I2C bus. */
  struct pbl_mag mag;
  /** Driver state. */
  struct pbl_mag3110_state *state;
  /** I2C device. */
  struct pbl_i2c_dev i2c;
  /** Data ready interrupt, active high, on an EXTI line. */
  struct pbl_gpio int_gpio;
  /** Sensor axis feeding each watch axis (0: X, 1: Y, 2: Z). */
  uint8_t axis_map[3];
  /** Direction of each watch axis: 1 or -1. */
  int8_t axis_dir[3];
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_mag_ops pbl_mag3110_ops;
/** @endcond */

/**
 * @brief Define a MAG3110.
 *
 * @param sym Symbol of the instance; the magnetometer is @c sym.mag.
 * @param _bus I2C bus.
 * @param _addr 7-bit I2C address.
 * @param ... Designated initializers of the remaining fields: @c int_gpio, @c axis_map and
 *            @c axis_dir.
 */
#define PBL_MAG3110_DEFINE(sym, _bus, _addr, ...)                        \
  PBL_MAG_STATE_DEFINE(sym);                                             \
  static struct pbl_mag3110_state sym##_mag3110_state;                   \
  const struct pbl_mag3110 sym = {                                       \
    .mag = PBL_MAG_INIT(sym, "mag3110", &(_bus)->dev, &pbl_mag3110_ops), \
    .state = &sym##_mag3110_state,                                       \
    .i2c = PBL_I2C_DEV(_bus, _addr),                                     \
    __VA_ARGS__,                                                         \
  };                                                                     \
  PBL_DEVICE_REGISTER(sym, &sym.mag.dev)

/** @} */

/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/accel.h>
#include <pbl/drivers/i2c.h>

/**
 * @defgroup drivers_accel_lis3dh LIS3DH
 * @ingroup drivers_accel
 * @brief ST LIS3DH @ref drivers_accel driver.
 *
 * The I2C device (I2C_LIS3DH), interrupt lines and axis mapping come from the board
 * (BOARD_CONFIG_ACCEL); the board defines a single instance.
 *
 * @code{.c}
 * PBL_LIS3DH_DEFINE(s_lis3dh, &s_i2c.bus);
 *
 * const struct pbl_accel *const ACCEL = &s_lis3dh.accel;
 * @endcode
 * @{
 */

/** @brief LIS3DH accelerometer. */
struct pbl_lis3dh {
  /** Accelerometer, child of the I2C bus. */
  struct pbl_accel accel;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_accel_ops pbl_lis3dh_ops;
/** @endcond */

/**
 * @brief Define a LIS3DH.
 *
 * @param sym Symbol of the instance; the accelerometer is @c sym.accel.
 * @param _bus I2C bus.
 */
#define PBL_LIS3DH_DEFINE(sym, _bus)                                             \
  PBL_ACCEL_STATE_DEFINE(sym);                                                   \
  const struct pbl_lis3dh sym = {                                                \
    .accel = PBL_ACCEL_INIT(sym, "lis3dh", &(_bus)->dev, &pbl_lis3dh_ops),       \
  };                                                                             \
  PBL_DEVICE_REGISTER(sym, &sym.accel.dev)

/** @} */

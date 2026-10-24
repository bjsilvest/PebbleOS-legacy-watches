/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/accel.h>

/**
 * @defgroup drivers_accel_bma255 BMA255
 * @ingroup drivers_accel
 * @brief Bosch BMA255 @ref drivers_accel driver.
 *
 * The SPI link, interrupt lines and axis mapping come from the board (BOARD_CONFIG_ACCEL and
 * bma255_gpio_init()); the board defines a single instance.
 *
 * @code{.c}
 * PBL_BMA255_DEFINE(s_bma255);
 *
 * const struct pbl_accel *const ACCEL = &s_bma255.accel;
 * @endcode
 * @{
 */

/** @brief BMA255 accelerometer. */
struct pbl_bma255 {
  /** Accelerometer. */
  struct pbl_accel accel;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_accel_ops pbl_bma255_ops;
/** @endcond */

/**
 * @brief Define a BMA255.
 *
 * @param sym Symbol of the instance; the accelerometer is @c sym.accel.
 */
#define PBL_BMA255_DEFINE(sym)                                        \
  PBL_ACCEL_STATE_DEFINE(sym);                                        \
  const struct pbl_bma255 sym = {                                     \
    .accel = PBL_ACCEL_INIT(sym, "bma255", nullptr, &pbl_bma255_ops), \
  };                                                                  \
  PBL_DEVICE_REGISTER(sym, &sym.accel.dev)

/** @} */

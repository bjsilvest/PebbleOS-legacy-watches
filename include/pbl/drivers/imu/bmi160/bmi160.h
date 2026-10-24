/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/accel.h>

/**
 * @defgroup drivers_accel_bmi160 BMI160
 * @ingroup drivers_accel
 * @brief Bosch BMI160 @ref drivers_accel driver.
 *
 * The SPI link, interrupt lines and axis mapping come from the board (BOARD_CONFIG_ACCEL and
 * bmi160_spi_init()); the board defines a single instance.
 *
 * @code{.c}
 * PBL_BMI160_DEFINE(s_bmi160);
 *
 * const struct pbl_accel *const ACCEL = &s_bmi160.accel;
 * @endcode
 * @{
 */

/** @brief BMI160 accelerometer. */
struct pbl_bmi160 {
  /** Accelerometer. */
  struct pbl_accel accel;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_accel_ops pbl_bmi160_ops;
/** @endcond */

/**
 * @brief Define a BMI160.
 *
 * @param sym Symbol of the instance; the accelerometer is @c sym.accel.
 */
#define PBL_BMI160_DEFINE(sym)                                        \
  PBL_ACCEL_STATE_DEFINE(sym);                                        \
  const struct pbl_bmi160 sym = {                                     \
    .accel = PBL_ACCEL_INIT(sym, "bmi160", nullptr, &pbl_bmi160_ops), \
  };                                                                  \
  PBL_DEVICE_REGISTER(sym, &sym.accel.dev)

/** @} */

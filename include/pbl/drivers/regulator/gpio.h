/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/regulator.h>

/**
 * @defgroup drivers_regulator_gpio GPIO regulator
 * @ingroup drivers_regulator
 * @brief @ref drivers_regulator rail switched by a single enable output.
 * @{
 */

/** @brief GPIO switched rail. */
struct pbl_gpio_regulator {
  /** Regulator. */
  struct pbl_regulator reg;
  /** Enable output, active while the rail is on. */
  struct pbl_gpio enable;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_regulator_ops pbl_gpio_regulator_ops;
/** @endcond */

/**
 * @brief Define a GPIO switched rail.
 *
 * The rail is a child of the port, which is initialized first.
 *
 * @param sym Symbol of the rail; the regulator is @c sym.reg.
 * @param _name Name.
 * @param _port Port of the enable output.
 * @param _pin Pin of the enable output.
 * @param _flags Wiring flags of the enable output.
 * @param _always_on Switched on at init and never off.
 */
#define PBL_GPIO_REGULATOR_DEFINE(sym, _name, _port, _pin, _flags, _always_on)                 \
  PBL_REGULATOR_STATE_DEFINE(sym);                                                             \
  const struct pbl_gpio_regulator sym = {                                                      \
    .reg = PBL_REGULATOR_INIT(sym, _name, &(_port)->dev, &pbl_gpio_regulator_ops, _always_on), \
    .enable = PBL_GPIO(_port, _pin, _flags),                                                   \
  };                                                                                           \
  PBL_DEVICE_REGISTER(sym, &sym.reg.dev)

/** @} */

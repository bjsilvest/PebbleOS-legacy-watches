/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/vibe.h>

/**
 * @defgroup drivers_vibe_gpio GPIO motor
 * @ingroup drivers_vibe
 * @brief @ref drivers_vibe driver for a motor switched on and off by a GPIO, with no strength.
 *
 * @code{.c}
 * PBL_VIBE_GPIO_DEFINE(s_vibe, PBL_GPIO(STM32_GPIOB, 0, 0));
 *
 * const struct pbl_vibe *const VIBE = &s_vibe.vibe;
 * @endcode
 * @{
 */

/** @brief GPIO-switched vibration motor. */
struct pbl_vibe_gpio {
  /** Vibration motor. */
  struct pbl_vibe vibe;
  /** Motor switch, active while it runs. */
  struct pbl_gpio ctl;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_vibe_ops pbl_vibe_gpio_ops;
/** @endcond */

/**
 * @brief Define a GPIO-switched vibration motor.
 *
 * @param sym Symbol of the instance; the motor is @c sym.vibe.
 * @param _ctl Motor switch (struct pbl_gpio).
 */
#define PBL_VIBE_GPIO_DEFINE(sym, _ctl)                                                    \
  PBL_VIBE_STATE_DEFINE(sym);                                                              \
  const struct pbl_vibe_gpio sym = {                                                       \
    .vibe = PBL_VIBE_INIT(sym, "vibe_gpio", nullptr, &pbl_vibe_gpio_ops, VIBE_STRENGTH_OFF), \
    .ctl = _ctl,                                                                           \
  };                                                                                       \
  PBL_DEVICE_REGISTER(sym, &sym.vibe.dev)

/** @} */

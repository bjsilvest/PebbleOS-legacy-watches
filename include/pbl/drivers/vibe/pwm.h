/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/pwm.h>
#include <pbl/drivers/vibe.h>

/**
 * @defgroup drivers_vibe_pwm PWM motor
 * @ingroup drivers_vibe
 * @brief @ref drivers_vibe driver for a single-direction motor driven by a PWM output.
 *
 * @code{.c}
 * PBL_VIBE_PWM_DEFINE(s_vibe, .pwm = {...}, .vsys_scale = 3300);
 *
 * const struct pbl_vibe *const VIBE = &s_vibe.vibe;
 * @endcode
 * @{
 */

/** @brief PWM-driven vibration motor. */
struct pbl_vibe_pwm {
  /** Vibration motor. */
  struct pbl_vibe vibe;
  /** PWM output. */
  PwmConfig pwm;
  /**
   * Supply voltage, in mV, at which the strength is not scaled; the duty cycle is scaled by
   * vsys_scale / supply so the strength does not drop as the battery discharges. 0 disables it.
   */
  uint16_t vsys_scale;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_vibe_ops pbl_vibe_pwm_ops;
/** @endcond */

/**
 * @brief Define a PWM vibration motor.
 *
 * @param sym Symbol of the instance; the motor is @c sym.vibe.
 * @param ... Designated initializers of the remaining fields: @c pwm and @c vsys_scale.
 */
#define PBL_VIBE_PWM_DEFINE(sym, ...)                                                      \
  PBL_VIBE_STATE_DEFINE(sym);                                                              \
  const struct pbl_vibe_pwm sym = {                                                        \
    .vibe = PBL_VIBE_INIT(sym, "vibe_pwm", nullptr, &pbl_vibe_pwm_ops, VIBE_STRENGTH_OFF), \
    __VA_ARGS__                                                                            \
  };                                                                                       \
  PBL_DEVICE_REGISTER(sym, &sym.vibe.dev)

/** @} */

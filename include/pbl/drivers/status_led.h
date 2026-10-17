/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

/**
 * @defgroup drivers_status_led Status LED
 * @ingroup drivers
 * @brief Charging indicator LED, on boards with @c CONFIG_STATUS_LED.
 * @{
 */

/** @brief What the status LED shows. */
typedef enum {
  /** Off. */
  StatusLedState_Off,
  /** Plugged in and charging. */
  StatusLedState_Charging,
  /** Plugged in and fully charged. */
  StatusLedState_FullyCharged,
} StatusLedState;

#ifdef CONFIG_STATUS_LED
/**
 * @brief Show a state on the status LED.
 *
 * @param state State to show.
 */
void status_led_set(StatusLedState state);
#else
static inline void status_led_set(StatusLedState state) {
  (void)state;
}
#endif

/** @} */

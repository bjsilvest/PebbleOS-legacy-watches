/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/device.h>
#include <pbl/drivers/input/gpio_keys.h>
#include <pbl/util/size.h>

/**
 * @defgroup drivers_input_stm32_keys STM32 keys
 * @ingroup drivers
 * @brief Keys on STM32F4 GPIOs, debounced as the stock firmware does it.
 *
 * Any edge starts TIM4, which samples every key each 2 ms until all are stable; a key must be
 * stable for 40 ms to be reported. The keys are described as for @ref drivers_input_gpio_keys,
 * whose debounce interval is not used, in the order of the stock bootloader's stuck button
 * counts: Back, Up, Select, Down. One instance only.
 *
 * @code{.c}
 * PBL_STM32_KEYS_DEFINE(BUTTONS, "buttons",
 *   PBL_GPIO_KEYS_KEY("Back", PBL_GPIO(STM32_GPIOG, 4, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_BACK, 40),
 *   ...);
 * @endcode
 * @{
 */

/** @brief Most keys of an instance. */
#define PBL_STM32_KEYS_MAX 4

/** @brief Keys on STM32 GPIOs. */
struct pbl_stm32_keys {
  /** Device. */
  struct pbl_device dev;
  /** Keys. */
  const struct pbl_gpio_keys_key *keys;
  /** Number of keys. */
  uint8_t num_keys;
};

/** @cond INTERNAL_HIDDEN */
int pbl_stm32_keys_init(const struct pbl_device *dev);
/** @endcond */

/**
 * @brief Define and register the keys.
 * @param sym Symbol of the struct pbl_stm32_keys.
 * @param _name Device name.
 * @param ... Keys, each a @ref PBL_GPIO_KEYS_KEY.
 */
#define PBL_STM32_KEYS_DEFINE(sym, _name, ...)                                      \
  static const struct pbl_gpio_keys_key sym##_keys[] = {__VA_ARGS__};               \
  _Static_assert(ARRAY_LENGTH(sym##_keys) <= PBL_STM32_KEYS_MAX, "too many keys"); \
  PBL_DEVICE_STATE_DEFINE(sym);                                                     \
  const struct pbl_stm32_keys sym = {                                               \
    .dev = PBL_DEVICE_INIT(sym, _name, pbl_stm32_keys_init, nullptr, nullptr),      \
    .keys = sym##_keys,                                                             \
    .num_keys = ARRAY_LENGTH(sym##_keys),                                           \
  };                                                                                \
  PBL_DEVICE_REGISTER(sym, &sym.dev)

/** @} */

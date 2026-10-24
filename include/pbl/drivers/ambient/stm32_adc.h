/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/als.h>
#include <pbl/drivers/gpio.h>

/**
 * @defgroup drivers_als_stm32_adc STM32 ADC
 * @ingroup drivers_als
 * @brief @ref drivers_als driver for an analog light sensor read through the STM32 ADC.
 *
 * @code{.c}
 * PBL_ALS_STM32_ADC_DEFINE(s_als, nullptr, .enable = PBL_GPIO(STM32_GPIOC, 0, 0),
 *                          .channel = ADC_Channel_13, .pin = PBL_GPIO(STM32_GPIOC, 3, 0),
 *                          .always_on = true);
 *
 * const struct pbl_als *const ALS = &s_als.als;
 * @endcode
 * @{
 */

/** @brief Analog light sensor on an STM32 ADC channel. */
struct pbl_als_stm32_adc {
  /** Ambient light sensor. */
  struct pbl_als als;
  /** Optional sensor enable output. */
  struct pbl_gpio enable;
  /** ADC channel (ADC_Channel_x). */
  uint8_t channel;
  /** Input pin. */
  struct pbl_gpio pin;
  /** Keep the sensor powered instead of only while reading or primed. */
  bool always_on;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_als_ops pbl_als_stm32_adc_ops;
/** @endcond */

/**
 * @brief Define an STM32 ADC light sensor.
 *
 * @param sym Symbol of the instance; the sensor is @c sym.als.
 * @param _scale Lux calibration (const struct pbl_als_scale *), or nullptr.
 * @param ... Designated initializers of the remaining fields: @c enable, @c channel, @c pin and
 *            @c always_on.
 */
#define PBL_ALS_STM32_ADC_DEFINE(sym, _scale, ...)                                        \
  PBL_ALS_STATE_DEFINE(sym);                                                              \
  const struct pbl_als_stm32_adc sym = {                                                  \
    .als = PBL_ALS_INIT(sym, "als_stm32_adc", nullptr, &pbl_als_stm32_adc_ops, (_scale)), \
    __VA_ARGS__                                                                           \
  };                                                                                      \
  PBL_DEVICE_REGISTER(sym, &sym.als.dev)

/** @} */

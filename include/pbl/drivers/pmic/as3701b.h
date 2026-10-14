/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/device.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>

/**
 * @defgroup drivers_pmic_as3701b AS3701B
 * @ingroup drivers
 * @brief ams AS3701B PMIC: charger, rails, display VCOM clock, and the
 * battery voltage measurement (through an STM32 ADC channel).
 * @{
 */

/** @brief AS3701B board configuration. */
struct pbl_as3701b {
  /** Device. */
  struct pbl_device dev;
  /** I2C device. */
  const struct pbl_i2c_dev *i2c;
  /** Interrupt pin (active low). */
  struct pbl_gpio int_gpio;
  /** ADC channel the battery divider is on. */
  uint8_t vbat_adc_channel;
  /** ADC input pin of the battery divider. */
  struct pbl_gpio vbat_adc_pin;
  /** Battery divider ratio: Vbat = Vpin * numerator / denominator. */
  uint32_t vbat_scale_numerator;
  uint32_t vbat_scale_denominator;
};

/** @brief The board's PMIC. */
extern const struct pbl_as3701b *const AS3701B;

/** @cond INTERNAL_HIDDEN */
int pbl_as3701b_init(const struct pbl_device *dev);
/** @endcond */

/** @brief Define the PMIC; the board also defines @c AS3701B to point at it. */
#define PBL_AS3701B_DEFINE(sym, _bus, _i2c, ...)                                     \
  PBL_DEVICE_STATE_DEFINE(sym);                                                      \
  const struct pbl_as3701b sym = {                                                   \
    .dev = PBL_DEVICE_INIT(sym, "as3701b", pbl_as3701b_init, &(_bus)->dev, nullptr), \
    .i2c = (_i2c),                                                                   \
    __VA_ARGS__                                                                      \
  };                                                                                 \
  PBL_DEVICE_REGISTER(sym, &sym.dev)

/** @} */

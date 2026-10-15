/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/device.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>

/**
 * @defgroup drivers_pmic_max14690 MAX14690
 * @ingroup drivers_pmic
 * @brief Maxim MAX14690 PMIC and battery measurement (snowy).
 * @{
 */

struct pbl_max14690 {
  /** Device. */
  struct pbl_device dev;
  /** I2C device. */
  const struct pbl_i2c_dev *i2c;
  /** Interrupt pin (active low). */
  struct pbl_gpio int_gpio;
  /** ADC channel of the PMIC's MON output. */
  uint8_t mon_adc_channel;
  /** ADC input pin of the PMIC's MON output. */
  struct pbl_gpio mon_adc_pin;
};

extern const struct pbl_max14690 *const MAX14690;

/** @cond INTERNAL_HIDDEN */
int pbl_max14690_init(const struct pbl_device *dev);
/** @endcond */

/** @brief Define a MAX14690 on an I2C bus. */
#define PBL_MAX14690_DEFINE(sym, _bus, _i2c, ...)                                      \
  PBL_DEVICE_STATE_DEFINE(sym);                                                        \
  const struct pbl_max14690 sym = {                                                    \
    .dev = PBL_DEVICE_INIT(sym, "max14690", pbl_max14690_init, &(_bus)->dev, nullptr), \
    .i2c = (_i2c),                                                                     \
    __VA_ARGS__                                                                        \
  };                                                                                   \
  PBL_DEVICE_REGISTER(sym, &sym.dev)

/** @} */

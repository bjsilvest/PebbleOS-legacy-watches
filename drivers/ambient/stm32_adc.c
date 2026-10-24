/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Analog ambient light sensor read through the STM32 ADC, as the stock firmware did it. Levels
//! are 12-bit.

#include <pbl/drivers/adc/stm32.h>
#include <pbl/drivers/als.h>
#include <pbl/drivers/ambient/stm32_adc.h>
#include <pbl/util/misc.h>

static const struct pbl_als_stm32_adc *prv_get(const struct pbl_als *als) {
  return container_of(als, const struct pbl_als_stm32_adc, als);
}

static void prv_sensor_enable(const struct pbl_als_stm32_adc *dev, bool enable) {
  if (pbl_gpio_is_connected(&dev->enable)) {
    (void)pbl_gpio_set(&dev->enable, enable);
  }
}

static int prv_init(const struct pbl_als *als) {
  const struct pbl_als_stm32_adc *dev = prv_get(als);
  int ret;

  if (pbl_gpio_is_connected(&dev->enable)) {
    ret = pbl_gpio_configure(&dev->enable, PBL_GPIO_OUTPUT_INACTIVE | PBL_GPIO_PULL_DOWN);
    if (ret != 0) {
      return ret;
    }
  }

  if (dev->always_on) {
    prv_sensor_enable(dev, true);
  }

  return 0;
}

static int prv_read(const struct pbl_als *als, uint32_t *level) {
  const struct pbl_als_stm32_adc *dev = prv_get(als);
  uint32_t vref_total;
  uint32_t vmon_total;

  if (!dev->always_on) {
    prv_sensor_enable(dev, true);
  }

  pbl_adc_stm32_read(dev->channel, &dev->pin, &vref_total, &vmon_total);

  if (!dev->always_on) {
    prv_sensor_enable(dev, false);
  }

  if (vref_total == 0U) {
    *level = 0U;
    return 0;
  }

  // vmon/vref * 2/3 is the fraction of full scale (VREFINT is 1.2V of 1.8V);
  // scale it to a 12-bit level.
  *level = (vmon_total * 4096U * 2U) / (vref_total * 3U);

  return 0;
}

static int prv_set_state(const struct pbl_als *als, bool active, bool sampling) {
  const struct pbl_als_stm32_adc *dev = prv_get(als);

  (void)sampling;

  if (!dev->always_on) {
    prv_sensor_enable(dev, active);
  }

  return 0;
}

const struct pbl_als_ops pbl_als_stm32_adc_ops = {
  .init = prv_init,
  .read = prv_read,
  .set_state = prv_set_state,
};

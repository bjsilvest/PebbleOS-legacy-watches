/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F412 internal temperature sensor (datasheet section 6.3.21).

#include <errno.h>

#include <pbl/drivers/adc/stm32.h>
#include <pbl/drivers/temperature.h>
#include <pbl/drivers/temperature/stm32.h>

#include <stm32f4xx.h>

#define MILLIVOLTS_REF   760   // V25
#define MILLIDEGREES_REF 25000 // at 25 C
// Avg slope 2.5 mV/C: millidegrees = mV * 2000 / 5
#define SLOPE_NUMERATOR   5
#define SLOPE_DENOMINATOR 2000

static int prv_read(const struct pbl_temp *temp, int32_t *millicelsius) {
  uint32_t vref_total;
  uint32_t vmon_total;

  pbl_adc_stm32_read(ADC_Channel_TempSensor, nullptr, &vref_total, &vmon_total);
  if (vref_total == 0U) {
    return -EIO;
  }

  const int32_t millivolts = pbl_adc_stm32_to_mv(vref_total, vmon_total, 1, 1);
  *millicelsius =
      MILLIDEGREES_REF + ((millivolts - MILLIVOLTS_REF) * SLOPE_DENOMINATOR) / SLOPE_NUMERATOR;

  return 0;
}

static const struct pbl_temp_ops s_ops = {
  .read = prv_read,
};

PBL_TEMP_STATE_DEFINE(pbl_temp_stm32);
const struct pbl_temp pbl_temp_stm32 =
    PBL_TEMP_INIT(pbl_temp_stm32, "temp", nullptr, nullptr, &s_ops);
PBL_DEVICE_REGISTER(pbl_temp_stm32, &pbl_temp_stm32.dev);

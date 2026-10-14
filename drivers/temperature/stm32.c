/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F412 internal temperature sensor (datasheet section 6.3.21).

#include <pbl/drivers/adc/stm32.h>
#include <pbl/drivers/temperature.h>

#include <stm32f4xx.h>

#define MILLIVOLTS_REF   760   // V25
#define MILLIDEGREES_REF 25000 // at 25 C
// Avg slope 2.5 mV/C: millidegrees = mV * 2000 / 5
#define SLOPE_NUMERATOR   5
#define SLOPE_DENOMINATOR 2000

void temperature_init(void) {
}

int32_t temperature_read(void) {
  uint32_t vref_total;
  uint32_t vmon_total;
  pbl_adc_stm32_read(ADC_Channel_TempSensor, nullptr, &vref_total, &vmon_total);

  if (vref_total == 0) {
    return MILLIDEGREES_REF;
  }

  const int32_t millivolts = pbl_adc_stm32_to_mv(vref_total, vmon_total, 1, 1);
  return MILLIDEGREES_REF + ((millivolts - MILLIVOLTS_REF) * SLOPE_DENOMINATOR) / SLOPE_NUMERATOR;
}

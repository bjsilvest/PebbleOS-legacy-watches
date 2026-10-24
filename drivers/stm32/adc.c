/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/adc/stm32.h>
#include <pbl/drivers/gpio/stm32.h>
#include <pbl/kernel/mutex.h>

#include <pbl/kernel/delay.h>
#include <stm32f4xx.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

static PBL_MUTEX_DEFINE(s_adc_mutex);

#ifdef CONFIG_SOC_STM32F205
//! The tintin battery divider (750k || 470k, ~290k) is at the 144-cycle limit
//! with a 12 MHz ADC clock; 480 cycles allow ~1M source impedance.
#define ADC_SAMPLE_TIME ADC_SampleTime_480Cycles
#else
#define ADC_SAMPLE_TIME ADC_SampleTime_144Cycles
#endif

//! A conversion pair takes ~12 us; if we are busy longer, ADC1 overruns.
//! On overrun, clear OVR and EOC and restart the group.
static bool prv_wait_for_conversion(void) {
  while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET) {
    if (ADC_GetFlagStatus(ADC1, ADC_FLAG_OVR) == SET) {
      ADC_ClearFlag(ADC1, ADC_FLAG_OVR);
      ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
      return false;
    }
  }
  return true;
}

void pbl_adc_stm32_read(uint8_t channel, const struct pbl_gpio *pin, uint32_t *vref_total,
                        uint32_t *vmon_total) {
  pbl_mutex_lock(&s_adc_mutex, PBL_FOREVER);

  if (pin && pbl_gpio_is_connected(pin)) {
    pbl_gpio_stm32_analog(pin);
  }

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
  ADC_TempSensorVrefintCmd(ENABLE);

  ADC_CommonInitTypeDef common;
  ADC_CommonStructInit(&common);
  common.ADC_Mode = ADC_Mode_Independent;
  common.ADC_Prescaler = ADC_Prescaler_Div4;
  common.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
  common.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
  ADC_CommonInit(&common);

  ADC_InitTypeDef init;
  ADC_StructInit(&init);
  init.ADC_Resolution = ADC_Resolution_12b;
  init.ADC_ScanConvMode = ENABLE;
  init.ADC_ContinuousConvMode = DISABLE;
  init.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
  init.ADC_DataAlign = ADC_DataAlign_Right;
  init.ADC_NbrOfConversion = 2;
  ADC_Init(ADC1, &init);

  ADC_RegularChannelConfig(ADC1, ADC_Channel_Vrefint, 1, ADC_SAMPLE_TIME);
  ADC_RegularChannelConfig(ADC1, channel, 2, ADC_SAMPLE_TIME);
  ADC_EOCOnEachRegularChannelCmd(ADC1, ENABLE);
  ADC_Cmd(ADC1, ENABLE);
  pbl_busy_wait_us(10); // tSTAB is 3 us, VREFINT tSTART 10 us

  uint32_t vref_sum = 0;
  uint32_t vmon_sum = 0;
  int i = 0;
  while (i < PBL_ADC_STM32_NUM_CONVERSIONS) {
    ADC_SoftwareStartConv(ADC1);
    if (!prv_wait_for_conversion()) {
      continue;
    }
    const uint16_t vref = ADC_GetConversionValue(ADC1);
    if (!prv_wait_for_conversion()) {
      continue;
    }
    const uint16_t vmon = ADC_GetConversionValue(ADC1);
    vref_sum += vref;
    vmon_sum += vmon;
    ++i;
  }

  ADC_Cmd(ADC1, DISABLE);
  ADC_TempSensorVrefintCmd(DISABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, DISABLE);

  pbl_mutex_unlock(&s_adc_mutex);

  *vref_total = vref_sum;
  *vmon_total = vmon_sum;
}

uint32_t pbl_adc_stm32_to_mv(uint32_t vref_total, uint32_t vmon_total, uint32_t numerator,
                             uint32_t denominator) {
  // 12-bit counts to mV at a 1.8V full scale: x * 1800 / 4095 = x * 40 / 91.
  const uint32_t vref_mv_sum = vref_total * 40 / 91;
  const uint32_t vmon_mv_sum = vmon_total * 40 / 91;

  if (vref_mv_sum == 0) {
    return 0;
  }

  // VREFINT is 1.2V: mon = 1800 * (vmon / vref) * (1200 / 1800).
  const uint32_t millivolts = (vmon_mv_sum * 1800 * 2) / (vref_mv_sum * 3);

  return millivolts * numerator / denominator;
}

/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/gpio.h>

/** @brief Samples summed by pbl_adc_stm32_read(). */
#define PBL_ADC_STM32_NUM_CONVERSIONS 40

/**
 * @brief Read an ADC1 channel against the internal reference.
 *
 * Samples VREFINT and the channel back to back, @ref PBL_ADC_STM32_NUM_CONVERSIONS
 * times, so supply ripple affects both alike.
 *
 * @param channel ADC channel (ADC_Channel_x).
 * @param pin Input pin, put in analog mode; not connected for internal channels.
 * @param vref_total Sum of the VREFINT samples.
 * @param vmon_total Sum of the channel samples.
 */
void pbl_adc_stm32_read(uint8_t channel, const struct pbl_gpio *pin, uint32_t *vref_total,
                        uint32_t *vmon_total);

/**
 * @brief Convert a pbl_adc_stm32_read() result to millivolts at the pin,
 * then scale it by @p numerator / @p denominator (e.g. a resistor divider).
 */
uint32_t pbl_adc_stm32_to_mv(uint32_t vref_total, uint32_t vmon_total, uint32_t numerator,
                             uint32_t denominator);

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Analog ambient light sensor read through the STM32 ADC (silk), as the
//! stock firmware did it. Levels are 12-bit; boards may convert them to lux.

#include <inttypes.h>

#include <pbl/drivers/adc/stm32.h>
#include <pbl/drivers/ambient_light.h>

#include <board/board.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

static uint32_t s_dark_threshold;
static bool s_initialized;

static void prv_sensor_enable(bool enable) {
  pbl_gpio_set(&BOARD_CONFIG.photo_en, enable);
}

void ambient_light_init(void) {
  PBL_ASSERTN(BOARD_CONFIG.ambient_light_dark_threshold != 0);
  s_dark_threshold = BOARD_CONFIG.ambient_light_dark_threshold;

  pbl_gpio_configure(&BOARD_CONFIG.photo_en, PBL_GPIO_OUTPUT_INACTIVE | PBL_GPIO_PULL_DOWN);
  if (BOARD_CONFIG.als_always_on) {
    prv_sensor_enable(true);
  }

  s_initialized = true;
  ambient_light_common_init();
}

uint32_t ambient_light_get_light_level(void) {
  if (!s_initialized) {
    return BOARD_CONFIG.ambient_light_dark_threshold;
  }

  if (!BOARD_CONFIG.als_always_on) {
    prv_sensor_enable(true);
  }

  uint32_t vref_total;
  uint32_t vmon_total;
  pbl_adc_stm32_read(BOARD_CONFIG.als_adc_channel, &BOARD_CONFIG.als_adc_pin, &vref_total,
                     &vmon_total);

  if (!BOARD_CONFIG.als_always_on) {
    prv_sensor_enable(false);
  }

  if (vref_total == 0) {
    return 0;
  }

  // vmon/vref * 2/3 is the fraction of full scale (VREFINT is 1.2V of 1.8V);
  // scale it to a 12-bit level.
  return (vmon_total * 4096 * 2) / (vref_total * 3);
}

void ambient_light_driver_set_state(bool active, bool sampling) {
  if (!BOARD_CONFIG.als_always_on) {
    prv_sensor_enable(active);
  }
}

uint32_t ambient_light_get_dark_threshold(void) {
  return s_dark_threshold;
}

void ambient_light_set_dark_threshold(uint32_t new_threshold) {
  PBL_ASSERTN(new_threshold <= AMBIENT_LIGHT_LEVEL_MAX);
  s_dark_threshold = new_threshold;
}

bool ambient_light_is_light(void) {
  // If the sensor is not enabled, always report dark. The threshold lives in
  // the lux domain (see ambient_light_level_to_lux).
  return s_initialized &&
         ambient_light_level_to_lux(ambient_light_get_light_level()) > s_dark_threshold;
}

AmbientLightLevel ambient_light_level_to_enum(uint32_t light_level) {
  if (!s_initialized) {
    return AmbientLightLevelUnknown;
  }

  const uint32_t k_delta_threshold = BOARD_CONFIG.ambient_k_delta_threshold;
  if (light_level < (s_dark_threshold - k_delta_threshold)) {
    return AmbientLightLevelVeryDark;
  } else if (light_level < s_dark_threshold) {
    return AmbientLightLevelDark;
  } else if (light_level < (s_dark_threshold + k_delta_threshold)) {
    return AmbientLightLevelLight;
  }
  return AmbientLightLevelVeryLight;
}

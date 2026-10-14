/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

// clang-format off
#include "display.h"

#include <stdint.h>

#include <pbl/drivers/button_id.h>
#include <pbl/drivers/gpio/stm32.h>
#include <pbl/input/input.h>

#include <stm32f4xx.h>
// clang-format on

#define GPIO_Port_NULL (nullptr)
#define GPIO_Pin_NULL  ((uint16_t)-1)

//! An EXTI line and the GPIO port routed to it.
typedef struct {
  uint8_t exti_port_source; ///< Port index, A = 0 (EXTI_PortSourceGPIOx)
  uint8_t exti_line;        ///< Line, which is also the pin number
} ExtiConfig;

//! EXTI line of a pin on an STM32 port.
static inline ExtiConfig pbl_exti_stm32_config(const struct pbl_gpio *gpio) {
  return (ExtiConfig){
    .exti_port_source = container_of(gpio->port, const struct pbl_gpio_stm32, port)->index,
    .exti_line = gpio->pin,
  };
}

typedef struct {
  // Ambient Light Configuration
  /////////////////////////////////////////////////////////////////////////////
  const uint32_t ambient_light_dark_threshold;
  const uint32_t ambient_k_delta_threshold;
  // Raw-count -> lux conversion: lux = (level - offset) * num / den.
  // den == 0 means no conversion available for this board.
  const uint32_t ambient_light_lux_dark_offset;
  const uint32_t ambient_light_lux_num;
  const uint32_t ambient_light_lux_den;
  const struct pbl_gpio photo_en;
  const bool als_always_on;

  const uint8_t backlight_on_percent; // percent of max possible brightness
} BoardConfig;

// Button Configuration
/////////////////////////////////////////////////////////////////////////////
typedef struct {
  const char *const name; ///< Name for debugging purposes.
  const struct pbl_gpio gpio;
  uint16_t code;
} ButtonConfig;

typedef struct {
  const ButtonConfig buttons[NUM_BUTTONS];
} BoardConfigButton;

// Power Configuration
/////////////////////////////////////////////////////////////////////////////
typedef struct {
  //! Percentage for watch only mode
  const uint8_t low_power_threshold;

  //! Approximate hours of battery life
  const uint16_t battery_capacity_hours;
} BoardConfigPower;

typedef struct {
  uint8_t default_motion_sensitivity;
} BoardConfigAccel;

typedef struct {
  int axes_offsets[3];
  bool axes_inverts[3];
} MagConfig;

typedef struct {
  const MagConfig mag_config;
} BoardConfigMag;

typedef struct {
  SPI_TypeDef *spi;
  uint32_t spi_rcc; ///< RCC_APB1Periph_SPIx
  uint8_t spi_af;   ///< GPIO_AF_SPIx

  const struct pbl_gpio mosi;
  const struct pbl_gpio clk;
  const struct pbl_gpio cs;

  const struct pbl_gpio on_ctrl;
} BoardConfigSharpDisplay;

typedef const struct UARTDevice UARTDevice;
typedef const struct HRMDevice HRMDevice;
typedef const struct MicDevice MicDevice;
typedef const struct QSPIPort QSPIPort;
typedef const struct QSPIFlash QSPIFlash;
typedef const struct AudioDevice AudioDevice;

void board_early_init(void);
void board_init(void);

// clang-format off
#include "board_definitions.h"
// clang-format on

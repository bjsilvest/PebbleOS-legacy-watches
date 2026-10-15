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
  //! ADC channel and input pin of an analog light sensor.
  const uint8_t als_adc_channel;
  const struct pbl_gpio als_adc_pin;

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

typedef enum {
  AccelThresholdLow,  ///< A sensitive state used for stationary mode
  AccelThresholdHigh, ///< The accelerometer's default sensitivity
  AccelThreshold_Num,
} AccelThreshold;

typedef struct {
  int axes_offsets[3];
  bool axes_inverts[3];
  uint32_t shake_thresholds[AccelThreshold_Num];
  uint32_t double_tap_threshold;
} AccelConfig;

typedef struct {
  const AccelConfig accel_config;
  const struct pbl_gpio accel_int_gpios[2];
  uint8_t default_motion_sensitivity;
} BoardConfigAccel;

typedef struct {
  int axes_offsets[3];
  bool axes_inverts[3];
} MagConfig;

typedef struct {
  const MagConfig mag_config;
} BoardConfigMag;

//! A timer channel driving a PWM output pin.
typedef struct {
  struct pbl_gpio output;
  TIM_TypeDef *timer;
  uint32_t rcc;    ///< RCC_APB1Periph_TIMx, or RCC_APB2Periph_TIMx if apb2
  bool apb2;       ///< The timer is on APB2 (TIM1, TIM8-11)
  uint8_t af;      ///< GPIO_AF_TIMx
  uint8_t channel; ///< 1-4
} PwmConfig;

typedef struct {
  const PwmConfig pwm;
  //! Optional enable output, active while the motor runs.
  const struct pbl_gpio ctl;
  //! The PWM drives an H-bridge: 50% is stopped, below runs it in reverse.
  bool hbridge;
  //! Supply voltage (mV) at which the duty cycle is not scaled; 0 to not scale.
  uint16_t vsys_scale;
} BoardConfigActuator;

typedef struct {
  SPI_TypeDef *spi;
  uint32_t spi_rcc; ///< RCC_APB1Periph_SPIx
  uint8_t spi_af;   ///< GPIO_AF_SPIx

  const struct pbl_gpio mosi;
  const struct pbl_gpio clk;
  const struct pbl_gpio cs;

  const struct pbl_gpio on_ctrl;
} BoardConfigSharpDisplay;

//! Display driven through an iCE40LP framebuffer FPGA on a write-only SPI.
typedef struct {
  SPI_TypeDef *spi;
  uint32_t spi_rcc; ///< RCC_APB2Periph_SPIx
  uint8_t spi_af;   ///< GPIO_AF_SPIx

  const struct pbl_gpio sclk;
  const struct pbl_gpio mosi;
  const struct pbl_gpio scs; ///< Active low

  const struct pbl_gpio creset; ///< Open drain, active high (released = run)
  const struct pbl_gpio cdone;
  const struct pbl_gpio busy;

  //! Panel supplies, in power-up order.
  const struct pbl_gpio rail_6v6; ///< VDDC
  const struct pbl_gpio rail_4v5; ///< VDDP

  //! SPI clock while programming the FPGA, then for frames.
  uint32_t base_spi_frequency;
  uint32_t fast_spi_frequency;
} BoardConfigIce40Display;

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

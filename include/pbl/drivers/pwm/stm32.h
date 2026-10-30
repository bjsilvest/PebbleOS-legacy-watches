/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/pwm.h>

#include <stm32f4xx.h>

/**
 * @defgroup drivers_pwm_stm32 STM32 timer PWM
 * @ingroup drivers_pwm
 * @brief @ref drivers_pwm controller driver for the output channels of an STM32F4 timer.
 *
 * Channels 0 to 3 are the timer's CH1 to CH4. They share one counter, so channels running at the
 * same time have the same period; applying another period fails with @c -EBUSY. The counter is
 * 16 bits, with the smallest prescaler that fits the period. The timer stops in STOP mode, so
 * STOP is held off while any channel runs. An output is handed to the timer only while its
 * channel runs, and driven to its inactive level otherwise.
 *
 * @code{.c}
 * PBL_PWM_STM32_DEFINE(s_tim12, "tim12", TIM12, RCC_APB1Periph_TIM12, false, GPIO_AF_TIM12,
 *                      PBL_GPIO(STM32_GPIOB, 14, 0));
 * @endcode
 * @{
 */

/** @cond INTERNAL_HIDDEN */
struct pbl_pwm_stm32_state {
  uint16_t prescaler;
  uint16_t reload;
  uint8_t running;
};
/** @endcond */

/** @brief STM32 timer PWM controller. */
struct pbl_pwm_stm32 {
  /** PWM controller. */
  struct pbl_pwm_controller ctrl;
  /** Driver runtime state. */
  struct pbl_pwm_stm32_state *state;
  /** Timer. */
  TIM_TypeDef *timer;
  /** RCC_APB1Periph_TIMx, or RCC_APB2Periph_TIMx if @ref apb2. */
  uint32_t rcc;
  /** The timer is on APB2 (TIM1, TIM8 to TIM11). */
  bool apb2;
  /** GPIO_AF_TIMx. */
  uint8_t af;
  /** Output of each channel; unconnected channels cannot be used. */
  struct pbl_gpio out[4];
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_pwm_controller_ops pbl_pwm_stm32_ops;
int pbl_pwm_stm32_init(const struct pbl_device *dev);
/** @endcond */

/**
 * @brief Define an STM32 timer PWM controller.
 *
 * @param sym Symbol of the controller.
 * @param _name Name.
 * @param _timer Timer, e.g. TIM12.
 * @param _rcc RCC enable bit of the timer.
 * @param _apb2 Whether the timer is on APB2.
 * @param _af Alternate function of the outputs.
 * @param ... Outputs of channels 0 to 3 (CH1 to CH4), from PBL_GPIO().
 */
#define PBL_PWM_STM32_DEFINE(sym, _name, _timer, _rcc, _apb2, _af, ...)                         \
  PBL_PWM_CONTROLLER_STATE_DEFINE(sym, 4);                                                      \
  static struct pbl_pwm_stm32_state sym##_stm32_state;                                          \
  const struct pbl_pwm_stm32 sym = {                                                            \
    .ctrl = PBL_PWM_CONTROLLER_INIT(sym, _name, pbl_pwm_stm32_init, nullptr, &pbl_pwm_stm32_ops), \
    .state = &sym##_stm32_state,                                                                \
    .timer = (_timer),                                                                          \
    .rcc = (_rcc),                                                                              \
    .apb2 = (_apb2),                                                                            \
    .af = (_af),                                                                                \
    .out = {__VA_ARGS__},                                                                       \
  };                                                                                            \
  PBL_DEVICE_REGISTER(sym, &sym.ctrl.dev)

/** @} */

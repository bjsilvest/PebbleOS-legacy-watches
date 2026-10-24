/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F4 timer PWM, as the stock firmware did it.

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/pwm.h>
#include <pbl/kernel/irq.h>
#include <pbl/soc/stm32/sleep.h>

#include <stm32f4xx.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

//! Timers whose output is enabled: APB1 by index (TIM2 = 0 ... TIM14 = 8),
//! APB2 from 9 (TIM1 = 9, TIM8 = 10, TIM9 = 25 ... TIM11 = 27).
static uint32_t s_enabled_mask;

static void prv_clock(const PwmConfig *pwm, bool enable) {
  if (pwm->apb2) {
    RCC_APB2PeriphClockCmd(pwm->rcc, enable ? ENABLE : DISABLE);
  } else {
    RCC_APB1PeriphClockCmd(pwm->rcc, enable ? ENABLE : DISABLE);
  }
}

//! Timers stop in STOP mode: block it while any PWM output is on.
static void prv_set_stop_blocked(const PwmConfig *pwm, bool enable) {
  const uintptr_t base = pwm->apb2 ? APB2PERIPH_BASE : APB1PERIPH_BASE;
  const uint32_t bit = 1U << ((pwm->apb2 ? 9 : 0) + ((((uintptr_t)pwm->timer) - base) >> 10));

  pbl_irq_lock();
  const bool was_enabled = s_enabled_mask & bit;
  if (enable && !was_enabled) {
    s_enabled_mask |= bit;
    soc_stm32_stop_block();
  } else if (!enable && was_enabled) {
    s_enabled_mask &= ~bit;
    soc_stm32_stop_release();
  }
  pbl_irq_unlock();
}

static void prv_oc_init(const PwmConfig *pwm, uint32_t pulse) {
  TIM_OCInitTypeDef oc;
  TIM_OCStructInit(&oc);
  oc.TIM_OCMode = TIM_OCMode_PWM1;
  oc.TIM_OutputState = TIM_OutputState_Enable;
  oc.TIM_Pulse = pulse;
  oc.TIM_OCPolarity = TIM_OCPolarity_High;

  switch (pwm->channel) {
    case 1:
      TIM_OC1Init(pwm->timer, &oc);
      TIM_OC1PreloadConfig(pwm->timer, TIM_OCPreload_Enable);
      break;
    case 2:
      TIM_OC2Init(pwm->timer, &oc);
      TIM_OC2PreloadConfig(pwm->timer, TIM_OCPreload_Enable);
      break;
    case 3:
      TIM_OC3Init(pwm->timer, &oc);
      TIM_OC3PreloadConfig(pwm->timer, TIM_OCPreload_Enable);
      break;
    case 4:
      TIM_OC4Init(pwm->timer, &oc);
      TIM_OC4PreloadConfig(pwm->timer, TIM_OCPreload_Enable);
      break;
    default:
      PBL_ASSERTN(false);
  }
}

static uint32_t prv_timer_clock(const PwmConfig *pwm) {
  // Timers run at 2x their APB clock when that APB is divided.
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);
  const uint32_t pclk = pwm->apb2 ? clocks.PCLK2_Frequency : clocks.PCLK1_Frequency;
  return pclk * ((clocks.HCLK_Frequency == pclk) ? 1 : 2);
}

void pwm_init(const PwmConfig *pwm, uint32_t resolution, uint32_t frequency) {
  prv_clock(pwm, true);

  TIM_TimeBaseInitTypeDef tim_config;
  TIM_TimeBaseStructInit(&tim_config);
  tim_config.TIM_Period = resolution - 1;
  tim_config.TIM_Prescaler = (prv_timer_clock(pwm) / frequency) - 1;
  tim_config.TIM_CounterMode = TIM_CounterMode_Up;
  tim_config.TIM_ClockDivision = 0;
  TIM_TimeBaseInit(pwm->timer, &tim_config);

  prv_oc_init(pwm, 0);
  TIM_ARRPreloadConfig(pwm->timer, ENABLE);

  prv_clock(pwm, false);

  // Idle low until enabled.
  pbl_gpio_configure(&pwm->output, PBL_GPIO_OUTPUT_LOW);
}

void pwm_set_duty_cycle(const PwmConfig *pwm, uint32_t duty_cycle) {
  prv_oc_init(pwm, duty_cycle);
}

void pwm_enable(const PwmConfig *pwm, bool enable) {
  if (enable) {
    pbl_gpio_stm32_af(&pwm->output, pwm->af, PBL_GPIO_PULL_DOWN, GPIO_Speed_100MHz);
    prv_clock(pwm, true);
    TIM_Cmd(pwm->timer, ENABLE);
    prv_set_stop_blocked(pwm, true);
  } else {
    TIM_Cmd(pwm->timer, DISABLE);
    prv_clock(pwm, false);
    // Force the pin low while off.
    pbl_gpio_configure(&pwm->output, PBL_GPIO_OUTPUT_LOW);
    prv_set_stop_blocked(pwm, false);
  }
}

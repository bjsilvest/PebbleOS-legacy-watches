/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F4 timer PWM, as the stock firmware did it.

#include <errno.h>

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/pwm/stm32.h>
#include <pbl/kernel/irq.h>
#include <pbl/soc/stm32/sleep.h>
#include <pbl/util/bits.h>
#include <pbl/util/math.h>
#include <pbl/util/misc.h>
#include <pbl/util/units.h>

#define RELOAD_MAX 0xFFFFU

static const struct pbl_pwm_stm32 *prv_pwm(const struct pbl_pwm_controller *ctrl) {
  return container_of(ctrl, const struct pbl_pwm_stm32, ctrl);
}

static void prv_clock(const struct pbl_pwm_stm32 *pwm, bool enable) {
  if (pwm->apb2) {
    RCC_APB2PeriphClockCmd(pwm->rcc, enable ? ENABLE : DISABLE);
  } else {
    RCC_APB1PeriphClockCmd(pwm->rcc, enable ? ENABLE : DISABLE);
  }
}

static uint32_t prv_timer_clock(const struct pbl_pwm_stm32 *pwm) {
  // Timers run at 2x their APB clock when that APB is divided.
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);
  const uint32_t pclk = pwm->apb2 ? clocks.PCLK2_Frequency : clocks.PCLK1_Frequency;
  return pclk * ((clocks.HCLK_Frequency == pclk) ? 1 : 2);
}

static int prv_timing(uint32_t clock, uint32_t period, uint32_t *prescaler, uint32_t *reload) {
  const uint64_t counts = ((uint64_t)clock * period) / PBL_NSEC_PER_SEC;
  const uint64_t psc = (counts > 0U) ? (counts - 1U) / (RELOAD_MAX + 1U) : 0U;

  if (counts < 2U || psc > 0xFFFFU) {
    return -EINVAL;
  }

  *prescaler = (uint32_t)psc;
  *reload = (uint32_t)(counts / (psc + 1U)) - 1U;

  return 0;
}

//! PWM mode 1 with preload, as TIM_OCxInit() and TIM_OCxPreloadConfig() set it,
//! without pulling in all four of them.
static void prv_oc_init(const struct pbl_pwm_stm32 *pwm, uint8_t channel, uint32_t pulse,
                        bool inversed) {
  TIM_TypeDef *tim = pwm->timer;
  volatile uint16_t *ccmr = (channel < 2U) ? &tim->CCMR1 : &tim->CCMR2;
  const uint32_t ccmr_shift = (channel & 1U) * 8U;
  const uint32_t ccer_shift = channel * 4U;

  tim->CCER &= ~(TIM_CCER_CC1E << ccer_shift);
  *ccmr = (*ccmr & ~(0xFFU << ccmr_shift)) |
          ((TIM_OCMode_PWM1 | TIM_OCPreload_Enable) << ccmr_shift);
  (&tim->CCR1)[channel] = pulse;
  tim->CCER = (tim->CCER & ~((TIM_CCER_CC1P | TIM_CCER_CC1NP) << ccer_shift)) |
              ((TIM_CCER_CC1E | (inversed ? TIM_CCER_CC1P : 0U)) << ccer_shift);
}

static int prv_apply(const struct pbl_pwm_controller *ctrl, uint8_t channel,
                     const struct pbl_pwm_state *state) {
  const struct pbl_pwm_stm32 *pwm = prv_pwm(ctrl);
  struct pbl_pwm_stm32_state *st = pwm->state;
  const struct pbl_gpio *out = &pwm->out[channel];
  const bool inversed = state->polarity == PBL_PWM_POLARITY_INVERSED;
  uint32_t prescaler;
  uint32_t reload;
  int ret;

  if (!pbl_gpio_is_connected(out)) {
    return -EINVAL;
  }

  const uint32_t clock = prv_timer_clock(pwm);
  ret = prv_timing(clock, state->period, &prescaler, &reload);
  if (ret != 0) {
    return ret;
  }

  const uint8_t others = st->running & ~PBL_BIT(channel);

  if (!state->enabled) {
    // Force the pin to its inactive level while off.
    ret = pbl_gpio_configure(out, inversed ? PBL_GPIO_OUTPUT_HIGH : PBL_GPIO_OUTPUT_LOW);
    if (ret != 0) {
      return ret;
    }

    if (st->running != 0U && others == 0U) {
      TIM_Cmd(pwm->timer, DISABLE);
      prv_clock(pwm, false);
      soc_stm32_stop_release();
    }
    st->running = others;

    return 0;
  }

  if (others != 0U && (prescaler != st->prescaler || reload != st->reload)) {
    return -EBUSY;
  }

  const uint32_t tick_hz = clock / (prescaler + 1U);
  const uint32_t pulse =
      MIN((uint32_t)(((uint64_t)state->duty_cycle * tick_hz) / PBL_NSEC_PER_SEC), reload + 1U);

  if (st->running == 0U) {
    // TIMx stops in STOP mode.
    soc_stm32_stop_block();
    prv_clock(pwm, true);

    // Up counting, no clock division, ARR preloaded; UG loads PSC and ARR now.
    pwm->timer->CR1 = TIM_CR1_ARPE;
    pwm->timer->ARR = reload;
    pwm->timer->PSC = prescaler;
    pwm->timer->EGR = TIM_PSCReloadMode_Immediate;

    st->prescaler = prescaler;
    st->reload = reload;
  }

  prv_oc_init(pwm, channel, pulse, inversed);

  if ((st->running & PBL_BIT(channel)) == 0U) {
    pbl_gpio_stm32_af(out, pwm->af, PBL_GPIO_PULL_DOWN, GPIO_Speed_100MHz);
  }

  if (st->running == 0U) {
    TIM_Cmd(pwm->timer, ENABLE);
  }
  st->running = others | PBL_BIT(channel);

  return 0;
}

int pbl_pwm_stm32_init(const struct pbl_device *dev) {
  const struct pbl_pwm_stm32 *pwm = container_of(dev, const struct pbl_pwm_stm32, ctrl.dev);
  int ret;

  for (uint8_t i = 0U; i < ARRAY_LENGTH(pwm->out); i++) {
    if (!pbl_gpio_is_connected(&pwm->out[i])) {
      continue;
    }

    // Idle low until enabled.
    ret = pbl_gpio_configure(&pwm->out[i], PBL_GPIO_OUTPUT_LOW);
    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

const struct pbl_pwm_controller_ops pbl_pwm_stm32_ops = {
  .apply = prv_apply,
};

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Button debouncing on STM32F4, as the stock firmware does it: any edge
//! starts TIM4, which samples the buttons every 2 ms until they are stable.

#include <pbl/drivers/exti.h>
#include <pbl/drivers/input/stm32_keys.h>
#include <pbl/input/input.h>
#include <pbl/kernel/irq.h>
#include <pbl/soc/stm32/sleep.h>
#include <pbl/util/bitops.h>

#include <board/board.h>
#include <stm32f4xx.h>
#include <system/rtc_registers.h>

#define TIMER_FREQUENCY_HZ 32000
// Sample the buttons every 2 ms
#define TIMER_PERIOD_TICKS 64
// A button must be stable for 20 samples (40 ms) to be accepted.
#define NUM_DEBOUNCE_SAMPLES 20

#define BUTTON_TIMER_IRQ_PRIO 14

static const struct pbl_stm32_keys *s_keys;
static bool s_timer_enabled;

static void prv_timer_handler(void);

static void prv_init_timer(void) {
  RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

  // APB1 timers run at 2x PCLK1 when APB1 is divided.
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);
  const uint32_t timer_clock =
      clocks.PCLK1_Frequency * ((clocks.HCLK_Frequency == clocks.PCLK1_Frequency) ? 1 : 2);

  TIM_TimeBaseInitTypeDef tim_config;
  TIM_TimeBaseStructInit(&tim_config);
  tim_config.TIM_Prescaler = (timer_clock / TIMER_FREQUENCY_HZ) - 1;
  tim_config.TIM_Period = TIMER_PERIOD_TICKS - 1;
  tim_config.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM4, &tim_config);

  TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
  pbl_irq_enable(PBL_IRQN(TIM4));

  RCC->APB1ENR &= ~RCC_APB1ENR_TIM4EN;
}

static void prv_disable_timer(void) {
  if (s_timer_enabled) {
    s_timer_enabled = false;
    TIM_ITConfig(TIM4, TIM_IT_Update, DISABLE);
    TIM_Cmd(TIM4, DISABLE);
    RCC->APB1ENR &= ~RCC_APB1ENR_TIM4EN;
    soc_stm32_stop_release();
  }
}

static void prv_enable_timer(void) {
  pbl_irq_lock();
  if (!s_timer_enabled) {
    s_timer_enabled = true;
    // TIM4 stops in STOP mode.
    soc_stm32_stop_block();
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM4, ENABLE);
  }
  pbl_irq_unlock();
}

static void prv_button_interrupt_handler(void) {
  prv_enable_timer();
}

static bool prv_is_pressed(uint8_t i) {
  return pbl_gpio_get(&s_keys->keys[i].gpio) > 0;
}

//! The stock bootloader counts boots with each button held, to detect stuck
//! buttons; a release clears that button's count.
static void prv_clear_stuck_button(uint8_t i) {
  pbl_irq_lock();
  const uint32_t reg = RTC_ReadBackupRegister(STUCK_BUTTON_REGISTER);
  const uint32_t updated = reg & ~(0xffU << (i << 3));
  if (reg != updated) {
    RTC_WriteBackupRegister(STUCK_BUTTON_REGISTER, updated);
  }
  pbl_irq_unlock();
}

int pbl_stm32_keys_init(const struct pbl_device *dev) {
  s_keys = container_of(dev, const struct pbl_stm32_keys, dev);

  bool any_pressed = false;
  for (uint8_t i = 0; i < s_keys->num_keys; ++i) {
    const struct pbl_gpio *gpio = &s_keys->keys[i].gpio;
    pbl_gpio_configure(gpio, PBL_GPIO_INPUT);
    const ExtiConfig config = pbl_exti_stm32_config(gpio);
    exti_configure_pin(config, ExtiTrigger_RisingFalling, prv_button_interrupt_handler);
    exti_enable(config);
    any_pressed |= prv_is_pressed(i);
  }

  prv_init_timer();

  // A button held since boot gives no edge, so start sampling now.
  if (any_pressed) {
    prv_enable_timer();
  }

  return 0;
}

static void prv_tim4_irq_handler(void) {
  if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET) {
    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    prv_timer_handler();
  }
}

PBL_IRQ_CONNECT(TIM4, BUTTON_TIMER_IRQ_PRIO, prv_tim4_irq_handler, , 0);

static void prv_timer_handler(void) {
  static uint32_t s_button_timers[PBL_STM32_KEYS_MAX];
  static uint32_t s_debounced_button_state;

  bool can_disable_timer = true;

  for (uint8_t i = 0; i < s_keys->num_keys; ++i) {
    const bool debounced_button_state = pbl_bitset32_get(&s_debounced_button_state, i);
    const bool is_pressed = prv_is_pressed(i);

    if (is_pressed == debounced_button_state) {
      s_button_timers[i] = 0;
      continue;
    }

    can_disable_timer = false;
    s_button_timers[i] += 1;

    if (s_button_timers[i] == NUM_DEBOUNCE_SAMPLES) {
      s_button_timers[i] = 0;
      pbl_bitset32_update(&s_debounced_button_state, i, is_pressed);
      if (!is_pressed) {
        prv_clear_stuck_button(i);
      }
      pbl_input_report_key(s_keys->keys[i].code, is_pressed, true);
    }
  }

  if (can_disable_timer) {
    pbl_irq_lock();
    prv_disable_timer();
    pbl_irq_unlock();
  }
}

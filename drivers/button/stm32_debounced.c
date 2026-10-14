/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Button debouncing on STM32F4, as the stock firmware does it: any edge
//! starts TIM4, which samples the buttons every 2 ms until they are stable.

#include <pbl/drivers/button.h>
#include <pbl/drivers/debounced_button.h>
#include <pbl/drivers/exti.h>
#include <pbl/input/input.h>
#include <pbl/kernel/irq.h>
#include <pbl/util/bitops.h>

#include <board/board.h>
#include <stm32f4xx.h>
#include <system/bootbits.h>
#include <system/reboot_reason.h>
#include <system/reset.h>
#include <system/rtc_registers.h>

#define TIMER_FREQUENCY_HZ 32000
// Sample the buttons every 2 ms
#define TIMER_PERIOD_TICKS 64
// A button must be stable for 20 samples (40 ms) to be accepted.
#define NUM_DEBOUNCE_SAMPLES 20

#define RESET_BUTTONS ((1 << BUTTON_ID_SELECT) | (1 << BUTTON_ID_BACK))

#define DEBOUNCE_SAMPLES_PER_SECOND (TIMER_FREQUENCY_HZ / TIMER_PERIOD_TICKS)

// Must stay below the PMIC's back-button reset (~8-11 s), with enough margin
// that a user trying to reset does not shut the watch down instead.
#define RESET_THRESHOLD_SAMPLES (5 * DEBOUNCE_SAMPLES_PER_SECOND)

#define BUTTON_TIMER_IRQ_PRIO 14

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
  }
}

static void prv_enable_timer(void) {
  pbl_irq_lock();
  if (!s_timer_enabled) {
    s_timer_enabled = true;
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

//! The stock bootloader counts boots with each button held, to detect stuck
//! buttons; a release clears that button's count.
static void prv_clear_stuck_button(ButtonId button_id) {
  pbl_irq_lock();
  const uint32_t reg = RTC_ReadBackupRegister(STUCK_BUTTON_REGISTER);
  const uint32_t updated = reg & ~(0xffU << (button_id << 3));
  if (reg != updated) {
    RTC_WriteBackupRegister(STUCK_BUTTON_REGISTER, updated);
  }
  pbl_irq_unlock();
}

void debounced_button_init(void) {
  button_init();

  for (int i = 0; i < NUM_BUTTONS; ++i) {
    const ExtiConfig config = pbl_exti_stm32_config(&BOARD_CONFIG_BUTTON.buttons[i].gpio);
    exti_configure_pin(config, ExtiTrigger_RisingFalling, prv_button_interrupt_handler);
    exti_enable(config);
  }

  prv_init_timer();

  // A button held since boot gives no edge, so start sampling now.
  if (button_get_state_bits() != 0) {
    prv_enable_timer();
  }
}

static void prv_tim4_irq_handler(void) {
  if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET) {
    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    prv_timer_handler();
  }
}

PBL_IRQ_CONNECT(TIM4, BUTTON_TIMER_IRQ_PRIO, prv_tim4_irq_handler, , 0);

static void prv_timer_handler(void) {
  static uint32_t s_button_timers[NUM_BUTTONS];
  static uint32_t s_debounced_button_state;

  bool can_disable_timer = true;

  for (int i = 0; i < NUM_BUTTONS; ++i) {
    const bool debounced_button_state = pbl_bitset32_get(&s_debounced_button_state, i);
    const bool is_pressed = button_is_pressed(i);

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
      pbl_input_report_key(BOARD_CONFIG_BUTTON.buttons[i].code, is_pressed, true);
    }
  }

#if !defined(CONFIG_MFG)
  // Select + Back held: reset. With Up held too, boot into PRF.
  static uint32_t s_hard_reset_timer;
  if ((s_debounced_button_state & RESET_BUTTONS) == RESET_BUTTONS) {
    s_hard_reset_timer += 1;
    can_disable_timer = false;

    if (s_hard_reset_timer > RESET_THRESHOLD_SAMPLES) {
      __disable_irq();
      const bool force_prf = (s_debounced_button_state & (1 << BUTTON_ID_UP));
      if (force_prf) {
        boot_bit_set(BOOT_BIT_FORCE_PRF);
      }
      RebootReason reason = {
        .code =
            force_prf ? RebootReasonCode_PrfResetButtonsHeld : RebootReasonCode_ResetButtonsHeld,
      };
      reboot_reason_set(&reason);
      // This back door must always work: hard reset, not system_reset().
      system_hard_reset();
    }
  } else {
    s_hard_reset_timer = 0;
  }
#endif

  if (can_disable_timer) {
    pbl_irq_lock();
    prv_disable_timer();
    pbl_irq_unlock();
  }
}

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Idle: WFI, or STOP mode (clocks off, low-power regulator) for longer gaps,
//! woken by the RTC wakeup timer or any EXTI line, as the stock firmware did.

#include <pbl/drivers/flash.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/compiler.h>
#include <pbl/kernel/idle.h>
#include <pbl/services/analytics/analytics.h>
#include <pbl/soc/stm32/sleep.h>
#include <pbl/util/math.h>

#include <kernel/util/idle.h>
#include <stm32f4xx.h>

//! Wake a little early to be back on the PLL before the deadline.
#define EARLY_WAKEUP_TICKS 2
#define MIN_STOP_TICKS     5
//! Bounded by the RTC wakeup counter (~4 s), and well below the ~8 s IWDG.
#define MAX_STOP_TICKS ((RTC_TICKS_HZ * 7) / 2)

//! STM32F2/F4 erratum: after waking from STOP or SLEEP the core can misbehave
//! depending on the bytes after "bx lr". Keep WFI 16-byte aligned and pad the
//! rest of the cache line with NOPs.
static PBL_NOINLINE PBL_NAKED void prv_do_wfi(void) {
  __asm volatile(
      ".align 4 \n"
      "wfi      \n"
      "nop      \n"
      "bx lr    \n"
      "nop      \n"
      "nop      \n"
      "nop      \n"
      "nop      \n"
      "nop      \n");
}

//! CPU analytics: core clock cycles spent in WFI, RTC ticks spent in STOP.
static uint64_t s_analytics_wfi_cycles;
static uint32_t s_analytics_stop_ticks;

static void prv_enter_stop(void) {
  flash_power_down_for_stop_mode();

  RCC->APB1ENR |= RCC_APB1ENR_PWREN;

  // Low-power regulator in STOP (plus low voltage, on the F412), not standby.
  uint32_t cr = PWR->CR;
  cr &= ~PWR_CR_PDDS;
  cr |= PWR_CR_LPDS | PWR_CR_LPLVDS;
  PWR->CR = cr;

  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
  __DSB();
  prv_do_wfi();
  __ISB();
  SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

  // STOP leaves us on the HSI: back to the PLL.
  RCC->CR |= RCC_CR_PLLON;
  while ((RCC->CR & RCC_CR_PLLRDY) == 0) {
  }
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
  }

  flash_power_up_after_stop_mode();
}

void pbl_soc_idle(pbl_tick_t max_ticks) {
  if (!idle_is_allowed()) {
    return;
  }

  __disable_irq();

  if (pbl_idle_confirm()) {
    if (max_ticks < MIN_STOP_TICKS || !rtc_alarm_is_initialized() || !soc_stm32_stop_is_allowed()) {
      // SysTick keeps counting core clocks in WFI and its interrupt ends the
      // WFI, so at most one reload happens in between.
      uint32_t start = SysTick->VAL;
      __DSB();
      prv_do_wfi();
      __ISB();
      uint32_t end = SysTick->VAL;
      s_analytics_wfi_cycles += (start >= end) ? start - end : start + SysTick->LOAD + 1 - end;
    } else {
      pbl_tick_t sleep_ticks = max_ticks - EARLY_WAKEUP_TICKS;
      if (sleep_ticks > MAX_STOP_TICKS) {
        sleep_ticks = MAX_STOP_TICKS;
      }

      // SysTick halts in STOP; account for the time from the RTC instead.
      SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
      rtc_alarm_set(sleep_ticks);

      prv_enter_stop();

      RtcTicks elapsed_ticks = rtc_alarm_get_elapsed_ticks();
      pbl_idle_slept(elapsed_ticks);
      s_analytics_stop_ticks += elapsed_ticks;
      SysTick->VAL = 0;
      SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    }
  }

  __enable_irq();
}

bool pbl_soc_tick_enable(void) {
  return false;
}

void pbl_analytics_external_collect_cpu_stats(void) {
  static RtcTicks s_last_ticks;

  __disable_irq();
  uint64_t wfi_cycles = s_analytics_wfi_cycles;
  uint32_t stop_ticks = s_analytics_stop_ticks;
  s_analytics_wfi_cycles = 0;
  s_analytics_stop_ticks = 0;
  __enable_irq();

  RtcTicks now_ticks = rtc_get_ticks();
  uint32_t total_ticks = (uint32_t)(now_ticks - s_last_ticks);
  s_last_ticks = now_ticks;
  if (total_ticks == 0) {
    return;
  }

  uint32_t wfi_ticks = (uint32_t)((wfi_cycles * RTC_TICKS_HZ) / SystemCoreClock);
  uint32_t sleep_ticks = MIN(wfi_ticks + stop_ticks, total_ticks);
  uint32_t wfi_pct = (uint32_t)((wfi_ticks * 10000ULL) / total_ticks);
  uint32_t stop_pct = (uint32_t)((stop_ticks * 10000ULL) / total_ticks);
  uint32_t running_pct = (uint32_t)(((total_ticks - sleep_ticks) * 10000ULL) / total_ticks);

  PBL_ANALYTICS_SET_UNSIGNED(cpu_running_pct, running_pct);
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep0_pct, wfi_pct);
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep1_pct, stop_pct);
  PBL_ANALYTICS_SET_UNSIGNED(cpu_sleep2_pct, 0);
}

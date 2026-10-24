/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F2 RTC (original Pebble), as the stock firmware drove it.
//!
//! The F2 calendar has no sub-second register, so the stock bootloader runs
//! it in "fast mode": with prescalers 7/3 the seconds count 1024 times a
//! second, and the time of day is a counter that wraps every 86400 ticks
//! (84 s). The wall clock is a time and counter value saved in backup
//! registers, which the bootloader and PRF read and write the same way.
//!
//! Kernel ticks still come from the kernel tick; the fast calendar measures
//! STOP mode and the wakeup timer, which runs off RTCCLK and not the
//! calendar, wakes the MCU from it.

#include <inttypes.h>
#include <stdio.h>

#include <pbl/drivers/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/logging/logging.h>
#include <pbl/services/regular_timer.h>
#include <pbl/services/time.h>

#include <stm32f4xx.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>
#include <system/rtc_registers.h>

PBL_LOG_MODULE_DEFINE(driver_rtc, CONFIG_DRIVER_RTC_LOG_LEVEL);

#define RTC_ASYNC_PRESCALER 7
#define RTC_SYNC_PRESCALER  3
//! Calendar "seconds" per real second in fast mode
#define HW_TICKS_HZ 1024u
static_assert(32768 / ((RTC_ASYNC_PRESCALER + 1) * (RTC_SYNC_PRESCALER + 1)) == HW_TICKS_HZ,
              "fast mode prescalers");

//! The time of day counter wraps after a "day" of fast seconds.
#define TICKS_IN_AN_INTERVAL (60 * 60 * 24)
//! How often the wall clock is saved to the backup registers.
#define SAVE_TIME_FREQUENCY (30 * HW_TICKS_HZ)

// EXTI line the RTC wakeup timer is wired to.
#define EXTI_LINE_RTC_WAKEUP (1U << 22)

// Wakeup counter clock, RTCCLK / 2: up to 4 s, longer than any STOP.
#define RTC_WAKEUP_HZ (32768 / 2)

typedef uint32_t RtcIntervalTicks;

//! Wall clock time at hardware tick s_time_tick_base.
static time_t s_time_base;
static int64_t s_time_tick_base;

//! Counter value at the last read, and the hardware ticks of the wraps seen.
static RtcIntervalTicks s_last_ticks;
static uint64_t s_coarse_ticks = 1;

static uint64_t s_alarm_set_ticks;
static bool s_alarm_initialized;

static uint8_t prv_bcd_to_byte(uint8_t value) {
  return (uint8_t)(((value >> 4) * 10) + (value & 0x0f));
}

static RtcIntervalTicks prv_get_interval_ticks(void) {
  const uint32_t tr = RTC->TR;
  const uint8_t hours = prv_bcd_to_byte((tr & (RTC_TR_HT | RTC_TR_HU)) >> 16);
  const uint8_t minutes = prv_bcd_to_byte((tr & (RTC_TR_MNT | RTC_TR_MNU)) >> 8);
  const uint8_t seconds = prv_bcd_to_byte(tr & (RTC_TR_ST | RTC_TR_SU));
  return (((hours * 60) + minutes) * 60) + seconds;
}

static RtcIntervalTicks prv_elapsed_ticks(RtcIntervalTicks before, RtcIntervalTicks after) {
  return (after >= before) ? (after - before) : ((TICKS_IN_AN_INTERVAL - before) + after);
}

static time_t prv_ticks_to_time(uint64_t ticks) {
  return s_time_base + (time_t)(((int64_t)ticks - s_time_tick_base) / HW_TICKS_HZ);
}

static void prv_save_exact(RtcIntervalTicks interval_ticks, time_t time) {
  RTC_WriteBackupRegister(CURRENT_TIME_REGISTER, (uint32_t)time);
  RTC_WriteBackupRegister(CURRENT_INTERVAL_TICKS_REGISTER, interval_ticks);
}

static void prv_save(RtcIntervalTicks interval_ticks) {
  // Floored to the last whole second
  const RtcIntervalTicks at_second = (interval_ticks / HW_TICKS_HZ) * HW_TICKS_HZ;
  prv_save_exact(at_second, prv_ticks_to_time(s_coarse_ticks + interval_ticks));
}

//! Must run with interrupts off.
static uint64_t prv_get_hw_ticks(void) {
  const RtcIntervalTicks ticks = prv_get_interval_ticks();
  const RtcIntervalTicks last = s_last_ticks;
  s_last_ticks = ticks;

  bool save = false;
  if (ticks < last) {
    s_coarse_ticks += TICKS_IN_AN_INTERVAL;
    save = true;
  } else if (prv_elapsed_ticks(RTC_ReadBackupRegister(CURRENT_INTERVAL_TICKS_REGISTER), ticks) >
             SAVE_TIME_FREQUENCY) {
    // Saving often keeps a reset just before a wrap from looking fresh.
    save = true;
  }
  if (save) {
    prv_save(ticks);
  }

  return s_coarse_ticks + ticks;
}

static uint64_t prv_hw_ticks(void) {
  pbl_irq_lock();
  const uint64_t ticks = prv_get_hw_ticks();
  pbl_irq_unlock();
  return ticks;
}

void rtc_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  PWR->CR |= PWR_CR_DBP;

  // The stock bootloader starts the LSE and puts the RTC in fast mode on
  // every boot; only do it here if it did not.
  const uint32_t prer = RTC->PRER;
  if (!(RCC->BDCR & RCC_BDCR_RTCEN) || ((prer >> 16) & 0x7f) != RTC_ASYNC_PRESCALER ||
      (prer & 0x7fff) != RTC_SYNC_PRESCALER) {
    PBL_LOG_WRN("RTC not in fast mode, setting it up");
    RCC_LSEConfig(RCC_LSE_ON);
    while (!(RCC->BDCR & RCC_BDCR_LSERDY)) {
    }
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
    RCC_RTCCLKCmd(ENABLE);
    RTC_InitTypeDef init;
    RTC_StructInit(&init);
    init.RTC_AsynchPrediv = RTC_ASYNC_PRESCALER;
    init.RTC_SynchPrediv = RTC_SYNC_PRESCALER;
    RTC_Init(&init);
  }
  RTC_WaitForSynchro();

  // Recover the wall clock the bootloader or the last firmware saved.
  const RtcIntervalTicks saved_ticks = RTC_ReadBackupRegister(CURRENT_INTERVAL_TICKS_REGISTER);
  const time_t saved_time = (time_t)RTC_ReadBackupRegister(CURRENT_TIME_REGISTER);
  const RtcIntervalTicks before = prv_get_interval_ticks();
  s_time_base = saved_time + (time_t)(prv_elapsed_ticks(saved_ticks, before) / HW_TICKS_HZ);
  s_time_tick_base = -(int64_t)(before % HW_TICKS_HZ);

  // Restart the counter from zero, as the stock firmware did, so it does not
  // wrap early.
  RTC_TimeTypeDef rtc_time;
  RTC_TimeStructInit(&rtc_time);
  RTC_SetTime(RTC_Format_BIN, &rtc_time);
  s_last_ticks = 0;
  prv_save_exact(TICKS_IN_AN_INTERVAL - (HW_TICKS_HZ - (before % HW_TICKS_HZ)),
                 prv_ticks_to_time(s_coarse_ticks));
}

void rtc_calibrate_frequency(uint32_t frequency) {
  // The factory RTC calibration lives in the factory registry, not read yet.
}

static void prv_resync_timer_callback(void *data) {
  // The counter wraps every 84 s; read it well within that to see every wrap.
  prv_hw_ticks();
}

void rtc_init_timers(void) {
  static RegularTimerInfo s_resync_timer = {.cb = prv_resync_timer_callback};
  regular_timer_add_multisecond_callback(&s_resync_timer, 30);
}

RtcTicks rtc_get_ticks(void) {
  static pbl_tick_t s_last_kernel_ticks;
  static RtcTicks s_wraps;

  pbl_irq_lock();
  const pbl_tick_t ticks = pbl_uptime_ticks();
  if (ticks < s_last_kernel_ticks) {
    s_wraps += (RtcTicks)1 << 32;
  }
  s_last_kernel_ticks = ticks;
  const RtcTicks ret = s_wraps + ticks;
  pbl_irq_unlock();

  return ret;
}

void rtc_set_time(time_t time) {
  pbl_irq_lock();
  s_time_base = time;
  s_time_tick_base = (int64_t)prv_get_hw_ticks();
  prv_save((RtcIntervalTicks)((uint64_t)s_time_tick_base - s_coarse_ticks));
  pbl_irq_unlock();
}

void rtc_set_time_tm(struct pbl_tm *time_tm) {
  rtc_set_time(pbl_mktime(time_tm));
}

void rtc_get_time_ms(time_t *out_seconds, uint16_t *out_ms) {
  pbl_irq_lock();
  const int64_t since_base = (int64_t)prv_get_hw_ticks() - s_time_tick_base;
  const time_t base = s_time_base;
  pbl_irq_unlock();

  *out_seconds = base + (time_t)(since_base / HW_TICKS_HZ);
  *out_ms = (uint16_t)(((since_base % HW_TICKS_HZ) * 1000) / HW_TICKS_HZ);
}

// Wakeup alarm, to leave STOP mode
///////////////////////////////////////////////////////////////////////////////

static void prv_rtc_wkup_irq_handler(void) {
  if (RTC_GetITStatus(RTC_IT_WUT) != RESET) {
    RTC_WakeUpCmd(DISABLE);
    RTC_ClearITPendingBit(RTC_IT_WUT);
  }
  EXTI->PR = EXTI_LINE_RTC_WAKEUP;
}

PBL_IRQ_CONNECT(RTC_WKUP, 14, prv_rtc_wkup_irq_handler, , 0);

void rtc_alarm_init(void) {
  RTC_ITConfig(RTC_IT_WUT, DISABLE);
  RTC_WakeUpCmd(DISABLE);
  RTC_WakeUpClockConfig(RTC_WakeUpClock_RTCCLK_Div2);

  EXTI->RTSR |= EXTI_LINE_RTC_WAKEUP;
  EXTI->FTSR &= ~EXTI_LINE_RTC_WAKEUP;
  EXTI->IMR |= EXTI_LINE_RTC_WAKEUP;
  pbl_irq_enable(PBL_IRQN(RTC_WKUP));

  s_alarm_initialized = true;
}

void rtc_alarm_set(RtcTicks num_ticks) {
  PBL_ASSERTN(s_alarm_initialized);

  // The first WUTF comes (WUT + 1) wakeup clock cycles after WUTE.
  uint32_t wakeup_counter = (uint32_t)((num_ticks * RTC_WAKEUP_HZ) / RTC_TICKS_HZ) - 1;
  PBL_ASSERTN(wakeup_counter < 65536);

  RTC_ITConfig(RTC_IT_WUT, DISABLE);
  RTC_WakeUpCmd(DISABLE);
  RTC_SetWakeUpCounter(wakeup_counter);
  RTC_ClearFlag(RTC_FLAG_WUTF);
  EXTI->PR = EXTI_LINE_RTC_WAKEUP;
  RTC_ClearITPendingBit(RTC_IT_WUT);
  s_alarm_set_ticks = prv_hw_ticks();
  RTC_WakeUpCmd(ENABLE);
  RTC_ITConfig(RTC_IT_WUT, ENABLE);
}

RtcTicks rtc_alarm_get_elapsed_ticks(void) {
  const uint64_t elapsed = prv_hw_ticks() - s_alarm_set_ticks;
  return (elapsed * RTC_TICKS_HZ) / HW_TICKS_HZ;
}

bool rtc_alarm_is_initialized(void) {
  return s_alarm_initialized;
}

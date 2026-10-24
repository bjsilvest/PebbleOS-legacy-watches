/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F4 RTC, clocked by the 32.768 kHz LSE.
//!
//! The calendar holds UTC with a two-digit year (20xx), and the timezone
//! lives in backup registers, both as the stock firmware and PRF expect.
//! Ticks come from the kernel tick (1024 Hz), extended to 64 bits.

#include <stdio.h>

#include <pbl/drivers/rtc.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/sched.h>
#include <pbl/logging/logging.h>
#include <pbl/services/time.h>

#include <pbl/kernel/delay.h>
#include <stm32f4xx.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>
#include <system/rtc_registers.h>

PBL_LOG_MODULE_DEFINE(driver_rtc, CONFIG_DRIVER_RTC_LOG_LEVEL);

#define LSE_FREQUENCY_HZ 32768

// LSE / (127 + 1) / (255 + 1) = 1 Hz calendar, 256 sub-second steps.
#define RTC_CLOCK_ASYNC_PRESCALER 127
#define RTC_CLOCK_SYNC_PRESCALER  255

//! How long to wait for the LSE to start. Empirically about 4 seconds.
#define LSE_READY_TIMEOUT_MS 5000

// EXTI line the RTC wakeup timer is wired to.
#define EXTI_LINE_RTC_WAKEUP (1U << 22)

// Wakeup counter clock, RTCCLK / 2. Limits alarms to ~4 s, which is fine as
// a regular timer fires every second.
#define RTC_WAKEUP_HZ (LSE_FREQUENCY_HZ / 2)

static bool s_rtc_wakeup_initialized;
static uint64_t s_alarm_set_time_ms;

static void prv_lse_start(void) {
  // The LSE normally keeps running through standby to keep the RTC ticking;
  // it only stops if the MCU loses power completely.
  if (RCC->BDCR & RCC_BDCR_LSERDY) {
    return;
  }

  PBL_LOG_INFO("Starting LSE oscillator");
  // Silk feeds the LSE from an external 32 kHz clock.
  RCC_LSEConfig(RCC_LSE_Bypass);
  for (int i = 0; i < LSE_READY_TIMEOUT_MS; ++i) {
    if (RCC->BDCR & RCC_BDCR_LSERDY) {
      PBL_LOG_INFO("LSE oscillator started after %d ms", i);
      return;
    }
    pbl_busy_wait_us(1000);
  }

  PBL_LOG_ERR("LSE oscillator did not start");
}

void rtc_init(void) {
  // Backup domain (RTC, backup registers) write access.
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  PWR->CR |= PWR_CR_DBP;

  prv_lse_start();

  // Only initialize the RTC if it isn't already running: it keeps time
  // across resets and is shared with the bootloader and PRF.
  if (!(RCC->BDCR & RCC_BDCR_RTCEN)) {
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
    RCC_RTCCLKCmd(ENABLE);

    RTC_InitTypeDef rtc_init_struct;
    RTC_StructInit(&rtc_init_struct);
    rtc_init_struct.RTC_AsynchPrediv = RTC_CLOCK_ASYNC_PRESCALER;
    rtc_init_struct.RTC_SynchPrediv = RTC_CLOCK_SYNC_PRESCALER;
    RTC_Init(&rtc_init_struct);
  }

  RTC_WaitForSynchro();
}

void rtc_calibrate_frequency(uint32_t frequency) {
}

void rtc_init_timers(void) {
}

RtcTicks rtc_get_ticks(void) {
  static pbl_tick_t s_last_ticks;
  static RtcTicks s_wraps;

  pbl_irq_lock();
  const pbl_tick_t ticks = pbl_uptime_ticks();
  if (ticks < s_last_ticks) {
    s_wraps += (RtcTicks)1 << 32;
  }
  s_last_ticks = ticks;
  const RtcTicks ret = s_wraps + ticks;
  pbl_irq_unlock();

  return ret;
}

bool rtc_sanitize_struct_tm(struct pbl_tm *t) {
  // The hardware only stores a two-digit year, so only 2000-2037 work (the
  // upper bound is time_t's 2038 limit). tm_year is years since 1900.
  if (t->tm_year < 100) {
    t->tm_year = 100;
    return true;
  } else if (t->tm_year > 137) {
    t->tm_year = 137;
    return true;
  }
  return false;
}

bool rtc_sanitize_time_t(time_t *t) {
  struct pbl_tm time_struct;
  pbl_gmtime_r(t, &time_struct);

  const bool result = rtc_sanitize_struct_tm(&time_struct);
  *t = pbl_mktime(&time_struct);

  return result;
}

void rtc_set_time(time_t time) {
  struct pbl_tm t;
  pbl_gmtime_r(&time, &t);

  PBL_ASSERTN(!rtc_sanitize_struct_tm(&t));

  RTC_TimeTypeDef rtc_time = {
    .RTC_Hours = t.tm_hour,
    .RTC_Minutes = t.tm_min,
    .RTC_Seconds = t.tm_sec,
  };

  RTC_DateTypeDef rtc_date = {
    .RTC_WeekDay = t.tm_wday == 0 ? RTC_Weekday_Sunday : t.tm_wday,
    .RTC_Month = t.tm_mon + 1,
    .RTC_Date = t.tm_mday,
    .RTC_Year = t.tm_year % 100,
  };

  RTC_SetTime(RTC_Format_BIN, &rtc_time);
  RTC_SetDate(RTC_Format_BIN, &rtc_date);
}

void rtc_set_time_tm(struct pbl_tm *time_tm) {
  rtc_set_time(pbl_mktime(time_tm));
}

void rtc_get_time_ms(time_t *out_seconds, uint16_t *out_ms) {
  uint32_t sub_seconds = 0;
  RTC_DateTypeDef rtc_date;
  RTC_TimeTypeDef rtc_time;

  // Read the time on both sides of the date and sub-seconds, and retry if a
  // second (and so possibly the date) rolled over in between. The second date
  // read also unlocks the shadow registers that reading TR/SSR locks.
  int max_loops = 4;
  while (--max_loops) {
    RTC_TimeTypeDef rtc_time_before;
    RTC_GetTime(RTC_Format_BIN, &rtc_time_before);
    RTC_GetDate(RTC_Format_BIN, &rtc_date);
    sub_seconds = RTC_GetSubSecond();
    RTC_GetTime(RTC_Format_BIN, &rtc_time);
    RTC_GetDate(RTC_Format_BIN, &rtc_date);
    if (rtc_time.RTC_Seconds == rtc_time_before.RTC_Seconds) {
      break;
    }
  }
  PBL_ASSERTN(max_loops > 0);

  struct pbl_tm current_time = {
    .tm_sec = rtc_time.RTC_Seconds,
    .tm_min = rtc_time.RTC_Minutes,
    .tm_hour = rtc_time.RTC_Hours,
    .tm_mday = rtc_date.RTC_Date,
    .tm_mon = rtc_date.RTC_Month - 1,
    .tm_year = rtc_date.RTC_Year + 100,
  };

  // Guard against invalid values left behind by other firmware, which could
  // otherwise cause a reboot loop.
  const bool sanitized = rtc_sanitize_struct_tm(&current_time);

  *out_seconds = pbl_mktime(&current_time);
  *out_ms = (uint16_t)(((RTC_CLOCK_SYNC_PRESCALER - sub_seconds) * 1000) /
                       (RTC_CLOCK_SYNC_PRESCALER + 1));

  if (sanitized) {
    rtc_set_time(*out_seconds);
  }
}

time_t rtc_get_time(void) {
  time_t seconds;
  uint16_t ms;

  rtc_get_time_ms(&seconds, &ms);
  return seconds;
}

void rtc_get_time_tm(struct pbl_tm *time_tm) {
  time_t t = rtc_get_time();
  pbl_localtime_r(&t, time_tm);
}

const char *rtc_get_time_string(char *buffer) {
  return time_t_to_string(buffer, rtc_get_time());
}

const char *time_t_to_string(char *buffer, time_t t) {
  struct pbl_tm time;
  pbl_localtime_r(&t, &time);
  pbl_strftime_r(buffer, TIME_STRING_BUFFER_SIZE, "%c", &time);
  return buffer;
}

// Timezone
///////////////////////////////////////////////////////////////////////////////

void rtc_set_timezone(TimezoneInfo *tzinfo) {
  const uint32_t *raw = (const uint32_t *)tzinfo;
  static_assert(sizeof(TimezoneInfo) <= 5 * sizeof(uint32_t), "RTC Set Timezone invalid data size");

  RTC_WriteBackupRegister(RTC_TIMEZONE_ABBR_START, raw[0]);
  RTC_WriteBackupRegister(RTC_TIMEZONE_ABBR_END_TZID_DSTID, raw[1]);
  RTC_WriteBackupRegister(RTC_TIMEZONE_GMTOFFSET, raw[2]);
  RTC_WriteBackupRegister(RTC_TIMEZONE_DST_START, raw[3]);
  RTC_WriteBackupRegister(RTC_TIMEZONE_DST_END, raw[4]);
}

void rtc_get_timezone(TimezoneInfo *tzinfo) {
  uint32_t *raw = (uint32_t *)tzinfo;

  raw[0] = RTC_ReadBackupRegister(RTC_TIMEZONE_ABBR_START);
  raw[1] = RTC_ReadBackupRegister(RTC_TIMEZONE_ABBR_END_TZID_DSTID);
  raw[2] = RTC_ReadBackupRegister(RTC_TIMEZONE_GMTOFFSET);
  raw[3] = RTC_ReadBackupRegister(RTC_TIMEZONE_DST_START);
  raw[4] = RTC_ReadBackupRegister(RTC_TIMEZONE_DST_END);
}

void rtc_timezone_clear(void) {
  RTC_WriteBackupRegister(RTC_TIMEZONE_ABBR_START, 0);
  RTC_WriteBackupRegister(RTC_TIMEZONE_ABBR_END_TZID_DSTID, 0);
  RTC_WriteBackupRegister(RTC_TIMEZONE_GMTOFFSET, 0);
  RTC_WriteBackupRegister(RTC_TIMEZONE_DST_START, 0);
  RTC_WriteBackupRegister(RTC_TIMEZONE_DST_END, 0);
}

uint16_t rtc_get_timezone_id(void) {
  return ((RTC_ReadBackupRegister(RTC_TIMEZONE_ABBR_END_TZID_DSTID) >> 16) & 0xFFFF);
}

bool rtc_is_timezone_set(void) {
  // True if the abbreviation has been set (including UNK for unknown)
  return (RTC_ReadBackupRegister(RTC_TIMEZONE_ABBR_START) != 0);
}

// Wakeup alarm, to leave STOP mode
///////////////////////////////////////////////////////////////////////////////

static uint64_t prv_get_time_ms_since_epoch(void) {
  time_t seconds;
  uint16_t milliseconds;
  rtc_get_time_ms(&seconds, &milliseconds);

  return ((uint64_t)seconds * 1000) + milliseconds;
}

static void prv_rtc_wkup_irq_handler(void) {
  if (RTC_GetITStatus(RTC_IT_WUT) != RESET) {
    RTC_WakeUpCmd(DISABLE);
    RTC_ClearITPendingBit(RTC_IT_WUT);
    EXTI->PR = EXTI_LINE_RTC_WAKEUP;
  }
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

  s_rtc_wakeup_initialized = true;
}

void rtc_alarm_set(RtcTicks num_ticks) {
  PBL_ASSERTN(s_rtc_wakeup_initialized);

  uint32_t wakeup_counter = num_ticks * RTC_WAKEUP_HZ / RTC_TICKS_HZ;

  // RM0402: the first WUTF comes (WUT + 1) wakeup clock cycles after WUTE.
  wakeup_counter -= 1;
  PBL_ASSERTN(wakeup_counter < 65536);

  RTC_ITConfig(RTC_IT_WUT, DISABLE);
  RTC_WakeUpCmd(DISABLE);
  RTC_SetWakeUpCounter(wakeup_counter);
  RTC_ClearFlag(RTC_FLAG_WUTF);
  EXTI->PR = EXTI_LINE_RTC_WAKEUP;
  RTC_ClearITPendingBit(RTC_IT_WUT);
  RTC_WakeUpCmd(ENABLE);
  RTC_ITConfig(RTC_IT_WUT, ENABLE);

  s_alarm_set_time_ms = prv_get_time_ms_since_epoch();
}

RtcTicks rtc_alarm_get_elapsed_ticks(void) {
  const uint64_t now = prv_get_time_ms_since_epoch();
  PBL_ASSERTN(now >= s_alarm_set_time_ms);

  return ((now - s_alarm_set_time_ms) * RTC_TICKS_HZ) / 1000;
}

bool rtc_alarm_is_initialized(void) {
  return s_rtc_wakeup_initialized;
}

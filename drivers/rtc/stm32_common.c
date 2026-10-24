/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! RTC parts shared by the STM32F2 and STM32F4 drivers: wall clock helpers
//! over rtc_get_time_ms(), and the timezone in backup registers, where the
//! stock firmware and PRF keep it.

#include <stdio.h>

#include <pbl/drivers/rtc.h>
#include <pbl/services/time.h>

#include <stm32f4xx.h>
#include <system/rtc_registers.h>

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

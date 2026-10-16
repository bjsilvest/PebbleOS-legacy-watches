/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Original Pebble MFG info. The stock factory wrote the watch color and RTC
//! calibration into the factory registry (MFG_INFO region), which is not read
//! yet; the model was never programmed. Nothing is ever written.

#include <pbl/logging/logging.h>

#include <mfg/mfg_info.h>

static void prv_refuse_write(const char *what) {
  PBL_LOG_WRN("Not writing stock MFG info (%s)", what);
}

WatchInfoColor mfg_info_get_watch_color(void) {
  return WATCH_INFO_COLOR_UNKNOWN;
}

void mfg_info_set_watch_color(WatchInfoColor color) {
  prv_refuse_write("color");
}

uint32_t mfg_info_get_rtc_freq(void) {
  return 0;
}

void mfg_info_set_rtc_freq(uint32_t rtc_freq) {
  prv_refuse_write("rtc_freq");
}

void mfg_info_get_model(char *buffer) {
  buffer[0] = '\0';
}

void mfg_info_set_model(const char *model) {
  prv_refuse_write("model");
}

uint8_t mfg_info_get_vibe_cali(void) {
  return MFG_INFO_VIBE_CALI_INVALID;
}

void mfg_info_set_vibe_cali(uint8_t cali) {
  prv_refuse_write("vibe_cali");
}

void mfg_info_update_constant_data(void) {
}

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Stock spalding MFG info: snowy's, plus the display offsets. Written at
//! the factory and never by this firmware: every setter is a no-op. The same
//! flash sector also holds the stock bootloader's FPGA bitstream, which stock
//! firmware rewrote along with the struct; never doing that keeps the
//! bootloader's screens working.

#include <string.h>

#include <pbl/drivers/flash.h>
#include <pbl/logging/logging.h>

#include <flash_region/flash_region.h>
#include <mfg/mfg_info.h>

//! Stock struct version; the layout below must match what the factory wrote.
#define STOCK_DATA_VERSION 3

typedef struct {
  uint32_t data_version;

  uint32_t color;
  uint32_t rtc_freq;
  char model[MFG_INFO_MODEL_STRING_LENGTH]; //!< Null terminated model string
  int8_t disp_offset_x;
  int8_t disp_offset_y;
} MfgData;

static MfgData prv_fetch_struct(void) {
  MfgData result;

  flash_read_bytes((uint8_t *)&result, FLASH_REGION_MFG_INFO_BEGIN, sizeof(result));

  switch (result.data_version) {
    case STOCK_DATA_VERSION:
      break;
    case 2:
      result.disp_offset_x = 0;
      result.disp_offset_y = 0;
      break;
    case 1:
      result.model[0] = '\0';
      result.disp_offset_x = 0;
      result.disp_offset_y = 0;
      break;
    default:
      return (MfgData){.data_version = STOCK_DATA_VERSION};
  }

  result.model[MFG_INFO_MODEL_STRING_LENGTH - 1] = '\0';
  return result;
}

static void prv_refuse_write(const char *what) {
  PBL_LOG_WRN("Not writing stock MFG info (%s)", what);
}

WatchInfoColor mfg_info_get_watch_color(void) {
  return prv_fetch_struct().color;
}

void mfg_info_set_watch_color(WatchInfoColor color) {
  prv_refuse_write("color");
}

uint32_t mfg_info_get_rtc_freq(void) {
  return prv_fetch_struct().rtc_freq;
}

void mfg_info_set_rtc_freq(uint32_t rtc_freq) {
  prv_refuse_write("rtc_freq");
}

void mfg_info_get_model(char *buffer) {
  MfgData data = prv_fetch_struct();
  strcpy(buffer, data.model);
}

void mfg_info_set_model(const char *model) {
  prv_refuse_write("model");
}

GPoint mfg_info_get_disp_offsets(void) {
  MfgData data = prv_fetch_struct();
  return GPoint(data.disp_offset_x, data.disp_offset_y);
}

uint8_t mfg_info_get_vibe_cali(void) {
  return MFG_INFO_VIBE_CALI_INVALID;
}

void mfg_info_set_vibe_cali(uint8_t cali) {
  prv_refuse_write("vibe_cali");
}

void mfg_info_update_constant_data(void) {
}

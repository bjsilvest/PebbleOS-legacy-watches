/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Original Pebble MFG info. The stock factory wrote the watch color and RTC
//! calibration into the legacy factory registry in the MFG_INFO region, read
//! here; the model was never programmed. Nothing is ever written.

#include <stdint.h>
#include <string.h>

#include <pbl/drivers/flash.h>
#include <pbl/kernel/compiler.h>
#include <pbl/logging/logging.h>

#include <flash_region/flash_region.h>
#include <mfg/mfg_info.h>

// The stock registry: copies of 10 records start on 4 KiB boundaries, the
// active one with this header. Values are big-endian.
#define REGISTRY_NUM_RECORDS 10
#define REGISTRY_STRIDE      0x1000U
static const uint8_t REGISTRY_ACTIVE_HEADER[] = {0xff, 0x00, 0xff};

typedef struct PBL_PACKED {
  uint8_t active;
  uint8_t key_length;
  uint8_t description;
  uint8_t value_length;
  uint8_t uuid[5]; // all zero for system records
  char key[16];
  uint8_t value[44];
} RegistryRecord;

static void prv_refuse_write(const char *what) {
  PBL_LOG_WRN("Not writing stock MFG info (%s)", what);
}

//! A big-endian uint32 system record, or 0 if there is none.
static uint32_t prv_get_uint32(const char *key) {
  const size_t key_len = strlen(key);
  const uint32_t size = sizeof(REGISTRY_ACTIVE_HEADER) + REGISTRY_NUM_RECORDS *
                        sizeof(RegistryRecord);
  for (uint32_t addr = FLASH_REGION_MFG_INFO_BEGIN; addr + size <= FLASH_REGION_MFG_INFO_END;
       addr += REGISTRY_STRIDE) {
    uint8_t header[sizeof(REGISTRY_ACTIVE_HEADER)];
    flash_read_bytes(header, addr, sizeof(header));
    if (memcmp(header, REGISTRY_ACTIVE_HEADER, sizeof(header)) != 0) {
      continue;
    }
    for (int i = 0; i < REGISTRY_NUM_RECORDS; i++) {
      RegistryRecord rec;
      flash_read_bytes((uint8_t *)&rec,
                       addr + sizeof(header) + (uint32_t)i * sizeof(RegistryRecord), sizeof(rec));
      static const uint8_t system_uuid[5] = {};
      if (rec.active == 1 && rec.key_length == key_len && rec.value_length == 4 &&
          memcmp(rec.key, key, key_len) == 0 &&
          memcmp(rec.uuid, system_uuid, sizeof(system_uuid)) == 0) {
        return ((uint32_t)rec.value[0] << 24) | ((uint32_t)rec.value[1] << 16) |
               ((uint32_t)rec.value[2] << 8) | rec.value[3];
      }
    }
    return 0;
  }
  return 0;
}

WatchInfoColor mfg_info_get_watch_color(void) {
  static int s_color = -1;
  if (s_color < 0) {
    s_color = (int)prv_get_uint32("mfg_color");
  }
  return (WatchInfoColor)s_color;
}

void mfg_info_set_watch_color(WatchInfoColor color) {
  prv_refuse_write("color");
}

uint32_t mfg_info_get_rtc_freq(void) {
  return prv_get_uint32("mfg_rtcfreq");
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

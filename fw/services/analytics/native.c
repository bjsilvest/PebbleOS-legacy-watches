/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/drivers/rtc.h>
#include <pbl/kernel/compiler.h>
#include <pbl/kernel/mutex.h>
#include <pbl/logging/logging.h>
#include <pbl/services/analytics/backend.h>
#include <pbl/services/data_logging/data_logging_service.h>
#include <pbl/util/build_id.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>
#include <pbl/util/uuid.h>

PBL_LOG_MODULE_DEFINE(service_analytics, CONFIG_SERVICE_ANALYTICS_LOG_LEVEL);

#define NATIVE_HEARTBEAT_RECORD_VERSION 3

/* Heartbeat record logged to DLS */
struct PBL_PACKED native_heartbeat_record {
  uint8_t version;
  uint64_t timestamp;
  uint8_t build_id[BUILD_ID_EXPECTED_LEN];
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key) uint32_t metric_##key;
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)   int32_t metric_##key;
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale) \
  uint32_t metric_##key;                                        \
  uint16_t metric_##key##_scale;
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale) \
  int32_t metric_##key;                                       \
  uint16_t metric_##key##_scale;
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)       uint32_t metric_##key;
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len) char metric_##key[(len) + 1];
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
};

/* The record is logged as a raw byte blob, so it must have no padding. */
static_assert(sizeof(struct native_heartbeat_record) ==
                  sizeof(uint8_t) + sizeof(uint64_t) + BUILD_ID_EXPECTED_LEN
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)               +sizeof(uint32_t)
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)                 +sizeof(int32_t)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale) +sizeof(uint32_t) + sizeof(uint16_t)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)   +sizeof(int32_t) + sizeof(uint16_t)
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)                  +sizeof(uint32_t)
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len)            +((len) + 1)
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
              ,
              "native_heartbeat_record must be packed (no padding)");

/* Type-specific internal index enums (dense, no gaps) */

enum native_integer_index {
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)               NATIVE_INTEGER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)                 NATIVE_INTEGER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale) NATIVE_INTEGER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)   NATIVE_INTEGER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len)
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
  NATIVE_INTEGER_COUNT,
};

enum native_timer_index {
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key) NATIVE_TIMER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len)
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
  NATIVE_TIMER_COUNT,
};

enum native_string_index {
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len) NATIVE_STRING_IDX_##key,
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
  NATIVE_STRING_COUNT,
};

/* Per key, in definition order (also the record order): its kind and its
 * index in the storage of that kind. */

enum native_kind {
  NATIVE_KIND_INTEGER,
  NATIVE_KIND_SCALED,
  NATIVE_KIND_TIMER,
  NATIVE_KIND_STRING,
};

static const uint8_t s_key_kind[] = {
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)               NATIVE_KIND_INTEGER,
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)                 NATIVE_KIND_INTEGER,
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale) NATIVE_KIND_SCALED,
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)   NATIVE_KIND_SCALED,
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)                  NATIVE_KIND_TIMER,
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len)            NATIVE_KIND_STRING,
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
};

static const uint8_t s_key_index[] = {
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)               NATIVE_INTEGER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)                 NATIVE_INTEGER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale) NATIVE_INTEGER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)   NATIVE_INTEGER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)                  NATIVE_TIMER_IDX_##key,
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len)            NATIVE_STRING_IDX_##key,
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
};

static_assert(ARRAY_LENGTH(s_key_kind) == PBL_ANALYTICS_KEY_COUNT, "one entry per key");

/* Scale of each scaled metric, in definition order */
static const uint16_t s_scales[] = {
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale) (scale),
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)   (scale),
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len)
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
};

/* Type-specific storage */

static int32_t s_integer_values[NATIVE_INTEGER_COUNT];

static struct {
  uint32_t value_ms;
  bool running;
  RtcTicks start_ticks;
} s_timers[NATIVE_TIMER_COUNT];

/* Per-string dedicated buffers (sized to declared length) */
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len) static char s_string_##key[(len) + 1];
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING

/* String pointer and length lookup tables */

static char *const s_string_ptrs[] = {
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len) s_string_##key,
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
};

static const uint8_t s_string_lens[] = {
#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key)
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len) (len),
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING
};

static PBL_MUTEX_DEFINE(s_mutex);
static DataLoggingSession *s_dls_session;

extern const ElfExternalNote TINTIN_BUILD_ID;

static void prv_record_metrics(struct native_heartbeat_record *record, bool reset) {
  uint32_t timer_value_ms[NATIVE_TIMER_COUNT];

  memset(record, 0, sizeof(*record));

  record->version = NATIVE_HEARTBEAT_RECORD_VERSION;
  record->timestamp = rtc_get_time();
  memcpy(record->build_id, &TINTIN_BUILD_ID.data[TINTIN_BUILD_ID.name_length],
         MIN(BUILD_ID_EXPECTED_LEN, TINTIN_BUILD_ID.data_length));

  RtcTicks now = rtc_get_ticks();
  for (size_t i = 0; i < NATIVE_TIMER_COUNT; i++) {
    timer_value_ms[i] = s_timers[i].value_ms;
    if (s_timers[i].running) {
      RtcTicks elapsed = now - s_timers[i].start_ticks;
      timer_value_ms[i] += (elapsed * 1000) / RTC_TICKS_HZ;
      if (reset) {
        s_timers[i].value_ms = timer_value_ms[i];
        s_timers[i].start_ticks = now;
      }
    }
  }

  /* Build heartbeat record from type-specific storage */
  uint8_t *out = record->build_id + BUILD_ID_EXPECTED_LEN;
  const uint16_t *scale = s_scales;
  for (size_t key = 0; key < ARRAY_LENGTH(s_key_kind); key++) {
    uint8_t idx = s_key_index[key];
    switch (s_key_kind[key]) {
    case NATIVE_KIND_TIMER:
      memcpy(out, &timer_value_ms[idx], sizeof(uint32_t));
      out += sizeof(uint32_t);
      break;
    case NATIVE_KIND_STRING:
      strncpy((char *)out, s_string_ptrs[idx], s_string_lens[idx]);
      out += s_string_lens[idx] + 1;
      break;
    default:
      memcpy(out, &s_integer_values[idx], sizeof(int32_t));
      out += sizeof(int32_t);
      if (s_key_kind[key] == NATIVE_KIND_SCALED) {
        memcpy(out, scale++, sizeof(uint16_t));
        out += sizeof(uint16_t);
      }
      break;
    }
  }

  if (reset) {
    /* Reset storage for next heartbeat period, keeping running timers active */
    memset(s_integer_values, 0, sizeof(s_integer_values));
    for (size_t i = 0; i < NATIVE_TIMER_COUNT; i++) {
      s_timers[i].value_ms = 0;
    }
    for (size_t i = 0; i < NATIVE_STRING_COUNT; i++) {
      s_string_ptrs[i][0] = '\0';
    }
  }
}

void pbl_analytics__native_init(void) {
}

void pbl_analytics__native_heartbeat(void) {
  struct native_heartbeat_record record;

  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  prv_record_metrics(&record, true);
  pbl_mutex_unlock(&s_mutex);

  if (s_dls_session == nullptr) {
    Uuid system_uuid = UUID_SYSTEM;

    s_dls_session = dls_create(DlsSystemTagAnalyticsNativeHeartbeat, DATA_LOGGING_BYTE_ARRAY,
                               sizeof(struct native_heartbeat_record), false, false, &system_uuid);
    if (s_dls_session == nullptr) {
      PBL_LOG_WRN("Native analytics DLS session unavailable");
      return;
    }
  }

  DataLoggingResult result = dls_log(s_dls_session, &record, 1);
  if (result != DATA_LOGGING_SUCCESS) {
    PBL_LOG_ERR("Native analytics DLS log failed: %d", result);
  }
}

static int prv_integer_index(enum pbl_analytics_key key) {
  return (s_key_kind[key] <= NATIVE_KIND_SCALED) ? s_key_index[key] : -1;
}

static void prv_set_signed(enum pbl_analytics_key key, int32_t signed_value) {
  int idx = prv_integer_index(key);
  if (idx < 0) {
    return;
  }
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  s_integer_values[idx] = signed_value;
  pbl_mutex_unlock(&s_mutex);
}

static void prv_set_unsigned(enum pbl_analytics_key key, uint32_t unsigned_value) {
  int idx = prv_integer_index(key);
  if (idx < 0) {
    return;
  }
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  s_integer_values[idx] = (int32_t)unsigned_value;
  pbl_mutex_unlock(&s_mutex);
}

static void prv_set_string(enum pbl_analytics_key key, const char *value) {
  if (s_key_kind[key] != NATIVE_KIND_STRING) {
    return;
  }
  uint8_t idx = s_key_index[key];
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  strncpy(s_string_ptrs[idx], value, s_string_lens[idx]);
  s_string_ptrs[idx][s_string_lens[idx]] = '\0';
  pbl_mutex_unlock(&s_mutex);
}

static void prv_timer_start(enum pbl_analytics_key key) {
  if (s_key_kind[key] != NATIVE_KIND_TIMER) {
    return;
  }
  uint8_t idx = s_key_index[key];
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  if (!s_timers[idx].running) {
    s_timers[idx].running = true;
    s_timers[idx].start_ticks = rtc_get_ticks();
  }
  pbl_mutex_unlock(&s_mutex);
}

static void prv_timer_stop(enum pbl_analytics_key key) {
  if (s_key_kind[key] != NATIVE_KIND_TIMER) {
    return;
  }
  uint8_t idx = s_key_index[key];
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  if (s_timers[idx].running) {
    RtcTicks elapsed = rtc_get_ticks() - s_timers[idx].start_ticks;
    s_timers[idx].value_ms += (int32_t)((elapsed * 1000) / RTC_TICKS_HZ);
    s_timers[idx].running = false;
  }
  pbl_mutex_unlock(&s_mutex);
}

static void prv_add(enum pbl_analytics_key key, int32_t amount) {
  int idx = prv_integer_index(key);
  if (idx < 0) {
    return;
  }
  pbl_mutex_lock(&s_mutex, PBL_FOREVER);
  s_integer_values[idx] += amount;
  pbl_mutex_unlock(&s_mutex);
}

const struct pbl_analytics_backend_ops pbl_analytics__native_ops = {
  .set_signed = prv_set_signed,
  .set_unsigned = prv_set_unsigned,
  .set_string = prv_set_string,
  .timer_start = prv_timer_start,
  .timer_stop = prv_timer_stop,
  .add = prv_add,
};

#ifdef CONFIG_SHELL
#include <pbl/shell/shell.h>

static int prv_cmd_metrics(const struct pbl_shell *sh, size_t argc, char **argv) {
  struct native_heartbeat_record record;

  prv_record_metrics(&record, false);

#define PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED(key) \
  pbl_shell_print(sh, STRINGIFY(key) "=%" PRIu32, record.metric_##key);
#define PBL_ANALYTICS_METRIC_DEFINE_SIGNED(key) \
  pbl_shell_print(sh, STRINGIFY(key) "=%" PRId32, record.metric_##key);
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED(key, scale)                              \
  pbl_shell_print(sh, STRINGIFY(key) "=%" PRIu32 ".%" PRIu32, record.metric_##key / (scale), \
                  record.metric_##key - (record.metric_##key / (scale)) * (scale));
#define PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED(key, scale)                                \
  pbl_shell_print(sh, STRINGIFY(key) "=%" PRId32 ".%" PRIu32, record.metric_##key / (scale), \
                  (uint32_t)(record.metric_##key - (record.metric_##key / (scale)) * (scale)));
#define PBL_ANALYTICS_METRIC_DEFINE_TIMER(key) \
  pbl_shell_print(sh, STRINGIFY(key) "=%" PRId32 " ms", record.metric_##key);
#define PBL_ANALYTICS_METRIC_DEFINE_STRING(key, len) \
  pbl_shell_print(sh, STRINGIFY(key) "=%s", record.metric_##key);
#include <pbl/services/analytics/analytics.def>
#undef PBL_ANALYTICS_METRIC_DEFINE_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_UNSIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_SCALED_SIGNED
#undef PBL_ANALYTICS_METRIC_DEFINE_TIMER
#undef PBL_ANALYTICS_METRIC_DEFINE_STRING

  return 0;
}

PBL_SHELL_SUBCMD_ADD(sub_analytics, metrics, nullptr, "Dump the current metrics", prv_cmd_metrics,
                     0, 0);
#endif

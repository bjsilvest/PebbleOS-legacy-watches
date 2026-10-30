/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! ams AS7000 heart rate sensor on the Pebble 2 HR. Runs the HRM application already in the
//! sensor's flash and never writes it.

#include <string.h>

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/hrm.h>
#include <pbl/drivers/hrm/as7000.h>
#include <pbl/drivers/watchdog.h>
#include <pbl/kernel/delay.h>
#include <pbl/logging/logging.h>
#include <pbl/services/hrm/hrm_manager.h>
#include <pbl/services/system_task.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

#include <applib/app_logging.h>
#include <board/board.h>
#include <mfg/mfg_info.h>

PBL_LOG_MODULE_DEFINE(driver_hrm_as7000, CONFIG_DRIVER_HRM_LOG_LEVEL);

// The datasheet asks for 250 ms after enabling before talking to the chip. Releasing the enable
// line sooner can leave it in its loader.
#define BOOT_DELAY_MS 250
// Time for the sensor to tear down its application after the enable line is released
#define SHUT_DOWN_DELAY_MS 1000
// Handshakes before the reported intervals are trusted
#define WARMUP_HANDSHAKES 2
// Seconds without a handshake before the sensor is power cycled
#define WATCHDOG_TIMEOUT_S 5
// The loader returns to a valid application about 12.7 s after it was entered, whether or not
// it is polled meanwhile
#define LOADER_TIMEOUT_MS 15000
// Failed boots before hrm_enable() gives up until reboot
#define MAX_BOOT_FAILURES 3

#define EXPECTED_PROTOCOL_VERSION_MAJOR 2

// The application wants the accel rate between 10 and 20 Hz, in mHz. The manager samples at 25 Hz
// and every other sample is passed on.
#define ACCEL_RATE_MILLIHZ (HRM_MANAGER_ACCEL_RATE_MILLIHZ / 2)
_Static_assert(ACCEL_RATE_MILLIHZ >= 10000 && ACCEL_RATE_MILLIHZ <= 20000,
               "AS7000 accel rate out of range");

// Presence detection thresholds, in units of 64 ADC counts, by case color
#define PRES_DETECT_THRSH_WHITE 78 // 5000 counts
#define PRES_DETECT_THRSH_BLACK 54 // 3500 counts

#define ADDR_INFO_START 0x00
// In the loader
#define ADDR_LOADER_STATUS         0x00
#define ADDR_APP_IDS 0x04
#define ADDR_ACCEL_SAMPLE_FREQ_MSB 0x08
// Host time of one sensor second, in 0.1 ms, for clock skew compensation
#define ADDR_HOST_ONE_SECOND_TIME_MSB 0x0a
#define ADDR_NUM_ACCEL_SAMPLES 0x0e
#define ADDR_NUM_PPG_SAMPLES 0x0f
#define ADDR_ACCEL_SAMPLE_IDX 0x14
#define ADDR_PRES_DETECT_THRSH 0x26
#define ADDR_LED_CURRENT_MSB 0x34
// Reading it releases the handshake line, so it must be the last access of a handshake
#define ADDR_SYNC 0x39

#define AS7000_STATUS_NO_ACCEL 0x04

enum {
  AS7000AppId_Idle = 0x00,
  AS7000AppId_Loader = 0x01,
  AS7000AppId_HRM = 0x02,
  AS7000AppId_PRV = 0x04,
};

#define AS7000_LOADER_STATUS_READY 0x00

// Signal quality index thresholds, upper bounds
#define SQI_EXCELLENT 2
#define SQI_GOOD 5
#define SQI_ACCEPTABLE 8
#define SQI_POOR 10
#define SQI_WORST 20
#define SQI_OFF_WRIST 254

#define HR_MIN_PLAUSIBLE_BPM 25
#define HR_MAX_PLAUSIBLE_BPM 240

static bool prv_read(HRMDevice *dev, uint8_t reg, void *buf, uint32_t len) {
  pbl_i2c_use(dev->i2c);
  int ret = pbl_i2c_read_register_block(dev->i2c, reg, len, buf);
  pbl_i2c_release(dev->i2c);
  return ret == 0;
}

static bool prv_write(HRMDevice *dev, uint8_t reg, const void *buf, uint32_t len) {
  pbl_i2c_use(dev->i2c);
  int ret = pbl_i2c_write_register_block(dev->i2c, reg, len, buf);
  pbl_i2c_release(dev->i2c);
  return ret == 0;
}

static bool prv_write_u8(HRMDevice *dev, uint8_t reg, uint8_t value) {
  return prv_write(dev, reg, &value, 1);
}

static bool prv_write_u16(HRMDevice *dev, uint8_t reg, uint16_t value) {
  const uint8_t buf[2] = {value >> 8, value & 0xff};
  return prv_write(dev, reg, buf, sizeof(buf));
}

#ifdef CONFIG_HRM_AS7000_PRV_PROBE
// Undocumented registers around the PPG block, where the PRV results may be
static const uint8_t s_probe_ranges[][2] = {{0x10, 4}, {0x20, 20}};

static AS7000Probe s_probe;

static void prv_hex(char *out, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    sniprintf(&out[i * 2], 3, "%02x", data[i]);
  }
}

// One reading as two app log lines, for `pebble logs` (app logging must be on)
static void prv_probe_app_log(const uint8_t *regs, const uint8_t third[AS7000_PROBE_BLOCK_LEN],
                              uint8_t status, uint8_t sqi) {
  const AS7000ProbeSample *sample =
      &s_probe.history[(s_probe.history_next + AS7000_PROBE_HISTORY - 1) % AS7000_PROBE_HISTORY];
  char first[AS7000_PROBE_BLOCK_LEN * 2 + 1];
  char second[AS7000_PROBE_BLOCK_LEN * 2 + 1];
  char third_hex[AS7000_PROBE_BLOCK_LEN * 2 + 1];
  char dump[20 * 2 + 1];

  prv_hex(first, sample->first, AS7000_PROBE_BLOCK_LEN);
  prv_hex(second, sample->second, AS7000_PROBE_BLOCK_LEN);
  prv_hex(third_hex, third, AS7000_PROBE_BLOCK_LEN);
  prv_hex(dump, &regs[4], 20);
  app_log(APP_LOG_LEVEL_INFO, "as7000", 0, "PRV n%lu b%u p%u s%02x q%u 12=%02x %s %s %s",
          (unsigned long)s_probe.handshakes, s_probe.bpm, s_probe.ppg_samples, status, sqi,
          sample->reg12, first, second, third_hex);
  app_log(APP_LOG_LEVEL_INFO, "as7000", 0, "PRV n%lu 20-33 %s", (unsigned long)s_probe.handshakes,
          dump);
}

static void prv_probe_read(HRMDevice *dev, uint8_t bpm, uint8_t status, uint8_t sqi) {
  uint8_t regs[AS7000_PROBE_NUM_REGS];
  uint8_t *pos = regs;

  for (size_t i = 0; i < ARRAY_LENGTH(s_probe_ranges); i++) {
    if (!prv_read(dev, s_probe_ranges[i][0], pos, s_probe_ranges[i][1])) {
      return;
    }
    pos += s_probe_ranges[i][1];
  }

  // Read the 0x2c block again at once, to tell a queue from latched values
  AS7000ProbeSample *sample = &s_probe.history[s_probe.history_next];
  sample->reg12 = regs[2];
  memcpy(sample->first, &regs[16], AS7000_PROBE_BLOCK_LEN);
  if (!prv_read(dev, 0x2c, sample->second, AS7000_PROBE_BLOCK_LEN)) {
    memset(sample->second, 0xff, AS7000_PROBE_BLOCK_LEN);
  }
  if (memcmp(sample->first, sample->second, AS7000_PROBE_BLOCK_LEN) != 0) {
    s_probe.advances++;
  }
  s_probe.history_next = (s_probe.history_next + 1) % AS7000_PROBE_HISTORY;
  s_probe.max_2c = MAX(s_probe.max_2c, regs[16]);

  for (int i = 0; i < AS7000_PROBE_NUM_REGS; i++) {
    if (s_probe.handshakes > 0 && regs[i] != s_probe.regs[i]) {
      s_probe.changed |= 1U << i;
    }
    s_probe.regs[i] = regs[i];
  }
  s_probe.bpm = bpm;
  s_probe.status = status;
  s_probe.sqi = sqi;
  // The sampling rate, to see whether PRV samples faster than HRM alone
  uint8_t ppg_samples;
  if (prv_read(dev, ADDR_NUM_PPG_SAMPLES, &ppg_samples, 1)) {
    s_probe.ppg_samples = ppg_samples;
    if (s_probe.handshakes == 0 || ppg_samples < s_probe.ppg_min) {
      s_probe.ppg_min = ppg_samples;
    }
    s_probe.ppg_max = MAX(s_probe.ppg_max, ppg_samples);
  }
  s_probe.handshakes++;

  if (app_log_is_bt_enabled()) {
    uint8_t third[AS7000_PROBE_BLOCK_LEN];
    if (!prv_read(dev, 0x2c, third, sizeof(third))) {
      memset(third, 0xff, sizeof(third));
    }
    prv_probe_app_log(regs, third, status, sqi);
  }
}
#endif

static void prv_put_be16(uint8_t *buf, int16_t value) {
  buf[0] = (uint16_t)value >> 8;
  buf[1] = (uint16_t)value & 0xff;
}

static HRMQuality prv_quality(uint8_t status, uint8_t sqi) {
  if (status & AS7000_STATUS_NO_ACCEL) {
    return HRMQuality_Worst;
  } else if (sqi <= SQI_EXCELLENT) {
    return HRMQuality_Excellent;
  } else if (sqi <= SQI_GOOD) {
    return HRMQuality_Good;
  } else if (sqi <= SQI_ACCEPTABLE) {
    return HRMQuality_Acceptable;
  } else if (sqi <= SQI_POOR) {
    return HRMQuality_Poor;
  } else if (sqi == SQI_OFF_WRIST) {
    return HRMQuality_OffWrist;
  }
  return HRMQuality_Worst;
}

// Pass every other 25 Hz sample on: the count first, then one burst per sample
static void prv_write_accel(HRMDevice *dev) {
  HRMAccelData *accel = hrm_manager_get_accel_data();
  const AccelRawData *samples[HRM_MANAGER_MAX_ACCEL_SAMPLES];
  uint8_t count = 0;

  for (uint32_t i = 0; i < accel->num_samples; i++) {
    dev->state->accel_skip_next = !dev->state->accel_skip_next;
    if (dev->state->accel_skip_next) {
      samples[count++] = &accel->data[i];
    }
  }

  dev->state->accel_sent = count;
  prv_write_u8(dev, ADDR_NUM_ACCEL_SAMPLES, count);
  for (uint8_t i = 0; i < count; i++) {
    // Accel samples are in mG, the sensor wants 0.5 mG
    uint8_t sample[7] = {i + 1};
    prv_put_be16(&sample[1], samples[i]->x * 2);
    prv_put_be16(&sample[3], samples[i]->y * 2);
    prv_put_be16(&sample[5], samples[i]->z * 2);
    prv_write(dev, ADDR_ACCEL_SAMPLE_IDX, sample, sizeof(sample));
  }
  hrm_manager_release_accel_data();
}

static void prv_handle_handshake(void *context) {
  HRMDevice *dev = context;
  HRMDeviceState *state = dev->state;
  HRMData data = {.features = HRMFeature_BPM};

  state->handshake_pending = false;

  pbl_mutex_lock(&state->lock, PBL_FOREVER);
  if (state->state != AS7000State_On) {
    pbl_mutex_unlock(&state->lock);
    return;
  }

  if (state->handshake_count > WARMUP_HANDSHAKES) {
    interval_timer_take_sample(&state->handshake_interval);
  }

  prv_write_accel(dev);

  struct PBL_PACKED {
    uint8_t led_current[2];
    uint8_t status;
    uint8_t bpm;
    uint8_t sqi;
  } regs = {};
  const bool read_ok = prv_read(dev, ADDR_LED_CURRENT_MSB, &regs, sizeof(regs));

  uint32_t average_ms;
  const uint32_t intervals = interval_timer_get(&state->handshake_interval, &average_ms);
  if (intervals == 2 || intervals == 10 || (intervals > 0 && intervals % 30 == 0)) {
    prv_write_u16(dev, ADDR_HOST_ONE_SECOND_TIME_MSB, average_ms * 10);
  }

#ifdef CONFIG_HRM_AS7000_PRV_PROBE
  if (read_ok) {
    prv_probe_read(dev, regs.bpm, regs.status, regs.sqi);
  }
#endif

  uint8_t sync;
  prv_read(dev, ADDR_SYNC, &sync, 1);
  state->handshake_count++;
  pbl_mutex_unlock(&state->lock);

  if (!read_ok) {
    return;
  }

  data.hrm_quality = prv_quality(regs.status, regs.sqi);
  if (data.hrm_quality != HRMQuality_OffWrist) {
    if (regs.bpm >= HR_MIN_PLAUSIBLE_BPM && regs.bpm <= HR_MAX_PLAUSIBLE_BPM) {
      data.hrm_bpm = regs.bpm;
    } else {
      data.hrm_quality = HRMQuality_Worst;
    }
  }

  // Log when the sensor starts or stops missing accel data, with what it was sent
  const bool no_accel = (regs.status & AS7000_STATUS_NO_ACCEL) != 0;
  if (no_accel != state->no_accel || (no_accel && state->handshake_count % 30 == 0)) {
    state->no_accel = no_accel;
    PBL_LOG_WRN("AS7000 %s accel data (sent %u, status 0x%x)", no_accel ? "lacks" : "has",
                state->accel_sent, regs.status);
  }
  PBL_LOG_DBG("AS7000 BPM %u status 0x%x sqi %u", regs.bpm, regs.status, regs.sqi);
  hrm_manager_new_data_cb(&data);
}

static void prv_handshake_isr(void) {
  HRMDevice *dev = HRM;

  dev->state->missed_seconds = 0;
  if (!dev->state->handshake_pending) {
    dev->state->handshake_pending =
        system_task_add_callback_from_isr_droppable(prv_handle_handshake, (void *)dev);
  }
}

static void prv_power_on(HRMDevice *dev);

// Caller holds the lock. Never releases the enable line within the boot delay.
static void prv_power_off(HRMDevice *dev) {
  HRMDeviceState *state = dev->state;

  if (state->state != AS7000State_Booting && state->state != AS7000State_On) {
    return;
  }

  if (state->state == AS7000State_On) {
    regular_timer_remove_callback(&state->watchdog);
    exti_disable(dev->int_exti);
  } else {
    new_timer_stop(state->boot_timer);
  }
  pbl_gpio_stm32_analog(&dev->int_gpio);

  const uint32_t elapsed_ms =
      (uint32_t)((rtc_get_ticks() - state->power_on_ticks) * 1000 / RTC_TICKS_HZ);
  if (elapsed_ms < BOOT_DELAY_MS) {
    pbl_sleep_ms(BOOT_DELAY_MS - elapsed_ms);
  }

  pbl_gpio_set(&dev->enable, false);
  if (dev->led_supply != nullptr) {
    (void)pbl_regulator_disable(dev->led_supply);
  }
  state->power_off_ticks = rtc_get_ticks();
  state->state = AS7000State_Off;
  PBL_LOG_DBG("AS7000 off");
}

static void prv_watchdog_system_cb(void *context) {
  HRMDevice *dev = context;

  pbl_mutex_lock(&dev->state->lock, PBL_FOREVER);
  if (dev->state->state == AS7000State_On && dev->state->missed_seconds >= WATCHDOG_TIMEOUT_S) {
    PBL_LOG_ERR("AS7000 stopped handshaking, power cycling");
    prv_power_off(dev);
    prv_power_on(dev);
  }
  pbl_mutex_unlock(&dev->state->lock);
}

static void prv_watchdog_cb(void *context) {
  HRMDevice *dev = context;

  if (++dev->state->missed_seconds == WATCHDOG_TIMEOUT_S) {
    system_task_add_callback(prv_watchdog_system_cb, context);
  }
}

// Wait up to timeout_ms for the handshake line to reach level
static bool prv_wait_int(HRMDevice *dev, bool level, int timeout_ms) {
  for (int i = 0; i < timeout_ms; i++) {
    if ((pbl_gpio_get_raw(&dev->int_gpio) > 0) == level) {
      return true;
    }
    system_task_watchdog_feed();
    pbl_sleep_ms(1);
  }
  return false;
}

// Caller holds the lock
static bool prv_start_hrm_app(HRMDevice *dev) {
  HRMDeviceState *state = dev->state;
  AS7000InfoRecord info;

  if (!prv_read(dev, ADDR_INFO_START, &info, sizeof(info))) {
    PBL_LOG_ERR("AS7000 not answering");
    return false;
  }

  // Anything other than an idle application (e.g. the loader) is left alone
  if (info.protocol_version_major != EXPECTED_PROTOCOL_VERSION_MAJOR ||
      info.application_id != AS7000AppId_Idle) {
    PBL_LOG_ERR("AS7000 unexpected protocol %u app 0x%x", info.protocol_version_major,
                info.application_id);
    return false;
  }

  if (!prv_write_u16(dev, ADDR_ACCEL_SAMPLE_FREQ_MSB, ACCEL_RATE_MILLIHZ) ||
      !prv_write_u8(dev, ADDR_PRES_DETECT_THRSH, state->pres_detect_thrsh)) {
    PBL_LOG_ERR("AS7000 configuration failed");
    return false;
  }

  pbl_gpio_configure(&dev->int_gpio, PBL_GPIO_INPUT | PBL_GPIO_PULL_UP);

  uint8_t apps = AS7000AppId_HRM;
#ifdef CONFIG_HRM_AS7000_PRV_PROBE
  // Ask for PRV too while an app wants HRV, until a boot with it fails
  if ((state->features & HRMFeature_HRV) && !s_probe.prv_failed) {
    apps |= AS7000AppId_PRV;
  }
  s_probe.requested = apps;
#endif
  if (!prv_write_u8(dev, ADDR_APP_IDS, apps)) {
    PBL_LOG_ERR("AS7000 failed to start the HRM app");
    return false;
  }

  // The line pulses high for a few ms as the idle app ends, then goes low once the HRM app runs
  if (!prv_wait_int(dev, true, 300)) {
    PBL_LOG_DBG("AS7000 missed the idle app ending");
  }
  if (!prv_wait_int(dev, false, 2000)) {
    PBL_LOG_ERR("AS7000 HRM app not ready");
#ifdef CONFIG_HRM_AS7000_PRV_PROBE
    if (apps & AS7000AppId_PRV) {
      s_probe.prv_failed = true;
    }
#endif
    return false;
  }

  uint8_t app_ids = 0;
  prv_read(dev, ADDR_APP_IDS, &app_ids, 1);
  PBL_LOG_DBG("AS7000 running apps 0x%x", app_ids);
#ifdef CONFIG_HRM_AS7000_PRV_PROBE
  s_probe.running = app_ids;
  s_probe.boots++;
#endif

  return true;
}

static void prv_boot_system_cb(void *context) {
  HRMDevice *dev = context;
  HRMDeviceState *state = dev->state;

  pbl_mutex_lock(&state->lock, PBL_FOREVER);
  if (state->state != AS7000State_Booting) {
    pbl_mutex_unlock(&state->lock);
    return;
  }

  if (!prv_start_hrm_app(dev)) {
    state->boot_failures++;
    prv_power_off(dev);
    pbl_mutex_unlock(&state->lock);
    return;
  }

  state->boot_failures = 0;
  state->state = AS7000State_On;
  state->handshake_pending = false;
  interval_timer_init(&state->handshake_interval, 900, 1100, 8);

  exti_configure_pin(dev->int_exti, ExtiTrigger_Falling, prv_handshake_isr);
  exti_enable(dev->int_exti);

  state->missed_seconds = 0;
  regular_timer_add_seconds_callback(&state->watchdog);

  PBL_LOG_DBG("AS7000 on");
  pbl_mutex_unlock(&state->lock);
}

static void prv_boot_timer_cb(void *context) {
  system_task_add_callback(prv_boot_system_cb, context);
}

// Caller holds the lock
static void prv_power_on(HRMDevice *dev) {
  HRMDeviceState *state = dev->state;

  // A restart (e.g. for new features) must let the last application tear down first
  if (state->power_off_ticks != 0) {
    const uint32_t off_ms =
        (uint32_t)((rtc_get_ticks() - state->power_off_ticks) * 1000 / RTC_TICKS_HZ);
    if (off_ms < SHUT_DOWN_DELAY_MS) {
      pbl_sleep_ms(SHUT_DOWN_DELAY_MS - off_ms);
    }
  }

  if (dev->led_supply != nullptr) {
    (void)pbl_regulator_enable(dev->led_supply);
  }
  pbl_gpio_set(&dev->enable, true);
  state->power_on_ticks = rtc_get_ticks();
  state->handshake_count = 0;
  state->no_accel = false;
  state->state = AS7000State_Booting;
  new_timer_start(state->boot_timer, BOOT_DELAY_MS, prv_boot_timer_cb, (void *)dev, 0);
}

static uint8_t prv_pres_detect_thrsh(void) {
  switch (mfg_info_get_watch_color()) {
    case WATCH_INFO_COLOR_PEBBLE_2_HR_BLACK:
    case WATCH_INFO_COLOR_PEBBLE_2_HR_FLAME:
      return PRES_DETECT_THRSH_BLACK;
    case WATCH_INFO_COLOR_PEBBLE_2_HR_WHITE:
    case WATCH_INFO_COLOR_PEBBLE_2_HR_LIME:
    case WATCH_INFO_COLOR_PEBBLE_2_HR_AQUA:
      return PRES_DETECT_THRSH_WHITE;
    default:
      return 1;
  }
}

static void prv_sleep_fed(uint32_t ms) {
  while (ms > 0) {
    const uint32_t step = MIN(ms, 50U);
    watchdog_feed();
    pbl_sleep_ms(step);
    ms -= step;
  }
}

// At init, with the enable line high: wait until the sensor runs an idle application
static bool prv_wait_app(HRMDevice *dev, uint32_t max_ms, AS7000InfoRecord *info) {
  for (uint32_t waited = 0; waited < max_ms; waited += 100) {
    if (prv_read(dev, ADDR_INFO_START, info, sizeof(*info)) &&
        info->application_id == AS7000AppId_Idle &&
        info->protocol_version_major == EXPECTED_PROTOCOL_VERSION_MAJOR) {
      return true;
    }
    prv_sleep_fed(100);
  }
  return false;
}

void hrm_init(HRMDevice *dev) {
  HRMDeviceState *state = dev->state;
  AS7000InfoRecord info;

  pbl_mutex_init(&state->lock);
  state->boot_timer = new_timer_create();
  state->watchdog = (RegularTimerInfo){
    .cb = prv_watchdog_cb,
    .cb_data = (void *)dev,
  };
  state->pres_detect_thrsh = prv_pres_detect_thrsh();

  pbl_gpio_stm32_analog(&dev->int_gpio);
  pbl_gpio_configure(&dev->enable, PBL_GPIO_OUTPUT_ACTIVE);
  pbl_sleep_ms(BOOT_DELAY_MS);
  const bool found = prv_read(dev, ADDR_INFO_START, &info, sizeof(info));
  if (found && info.application_id == AS7000AppId_Loader) {
    // Give the loader its timeout to return to a valid application. The enable line must stay
    // high meanwhile, or the loader stays in force loader mode.
    PBL_LOG_WRN("AS7000 in its loader, waiting for the application");
    prv_wait_app(dev, LOADER_TIMEOUT_MS, &info);
  }
  pbl_gpio_set(&dev->enable, false);
  state->power_off_ticks = rtc_get_ticks();

  if (!found) {
    // A Pebble 2 SE
    PBL_LOG_DBG("No AS7000");
    state->state = AS7000State_Absent;
    return;
  }

  state->info = info;
  PBL_LOG_INFO("AS7000 protocol %u.%u SW %u.%u.%u HW %u app 0x%x", info.protocol_version_major,
               info.protocol_version_minor, info.sw_version_major >> 6,
               info.sw_version_major & 0x3f, info.sw_version_minor, info.hw_revision,
               info.application_id);

  if (info.application_id == AS7000AppId_Loader ||
      info.protocol_version_major != EXPECTED_PROTOCOL_VERSION_MAJOR) {
    // No usable application in the sensor flash
    PBL_LOG_ERR("AS7000 has no usable firmware");
    state->state = AS7000State_Absent;
    return;
  }

  state->state = AS7000State_Off;
}

bool hrm_is_present(HRMDevice *dev) {
  return dev->state->state != AS7000State_Uninitialized &&
         dev->state->state != AS7000State_Absent;
}

bool hrm_enable(HRMDevice *dev, HRMFeature features, bool low_latency) {
  HRMDeviceState *state = dev->state;
  bool ret = true;

  (void)low_latency;

  if (!hrm_is_present(dev)) {
    return false;
  }

  pbl_mutex_lock(&state->lock, PBL_FOREVER);
  if (state->state == AS7000State_Off) {
    if (state->boot_failures >= MAX_BOOT_FAILURES) {
      ret = false;
    } else {
      state->features = features;
      prv_power_on(dev);
    }
  }
  pbl_mutex_unlock(&state->lock);

  return ret;
}

void hrm_disable(HRMDevice *dev) {
  if (!hrm_is_present(dev)) {
    return;
  }

  pbl_mutex_lock(&dev->state->lock, PBL_FOREVER);
  prv_power_off(dev);
  pbl_mutex_unlock(&dev->state->lock);
}

bool hrm_is_enabled(HRMDevice *dev) {
  return dev->state->state == AS7000State_Booting || dev->state->state == AS7000State_On;
}

void hrm_set_activity_scene(HRMDevice *dev, HRMActivityScene scene) {
  (void)dev;
  (void)scene;
}

#ifdef CONFIG_HRM_AS7000_PRV_PROBE
bool as7000_get_probe(HRMDevice *dev, AS7000Probe *probe) {
  pbl_mutex_lock(&dev->state->lock, PBL_FOREVER);
  *probe = s_probe;
  pbl_mutex_unlock(&dev->state->lock);
  return hrm_is_present(dev);
}
#endif

bool as7000_get_version_info(HRMDevice *dev, AS7000InfoRecord *info) {
  *info = dev->state->info;
  return hrm_is_present(dev);
}

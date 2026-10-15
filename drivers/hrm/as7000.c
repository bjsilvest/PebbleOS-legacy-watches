/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! ams AS7000 heart rate sensor on the Pebble 2 HR. Runs the HRM application already in the
//! sensor's flash: the loader is never entered and nothing is ever written to the sensor flash.

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/hrm.h>
#include <pbl/drivers/hrm/as7000.h>
#include <pbl/logging/logging.h>
#include <pbl/services/hrm/hrm_manager.h>
#include <pbl/services/system_task.h>
#include <pbl/util/size.h>

#include <board/board.h>
#include <kernel/util/sleep.h>
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
#define ADDR_APP_IDS 0x04
#define ADDR_ACCEL_SAMPLE_FREQ_MSB 0x08
// Host time of one sensor second, in 0.1 ms, for clock skew compensation
#define ADDR_HOST_ONE_SECOND_TIME_MSB 0x0a
#define ADDR_NUM_ACCEL_SAMPLES 0x0e
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

static void prv_probe_read(HRMDevice *dev, uint8_t bpm) {
  uint8_t regs[AS7000_PROBE_NUM_REGS];
  uint8_t *pos = regs;

  for (size_t i = 0; i < ARRAY_LENGTH(s_probe_ranges); i++) {
    if (!prv_read(dev, s_probe_ranges[i][0], pos, s_probe_ranges[i][1])) {
      return;
    }
    pos += s_probe_ranges[i][1];
  }

  for (int i = 0; i < AS7000_PROBE_NUM_REGS; i++) {
    if (s_probe.handshakes > 0 && regs[i] != s_probe.regs[i]) {
      s_probe.changed |= 1U << i;
    }
    s_probe.regs[i] = regs[i];
  }
  s_probe.bpm = bpm;
  s_probe.handshakes++;
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
    prv_probe_read(dev, regs.bpm);
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
    psleep(BOOT_DELAY_MS - elapsed_ms);
  }

  pbl_gpio_set(&dev->enable, false);
  if (dev->led_supply != nullptr) {
    (void)pbl_regulator_disable(dev->led_supply);
  }
  state->state = AS7000State_Off;
  PBL_LOG_DBG("AS7000 off");
}

static void prv_watchdog_system_cb(void *context) {
  HRMDevice *dev = context;

  pbl_mutex_lock(&dev->state->lock, PBL_FOREVER);
  if (dev->state->state == AS7000State_On && dev->state->missed_seconds >= WATCHDOG_TIMEOUT_S) {
    PBL_LOG_ERR("AS7000 stopped handshaking, power cycling");
    prv_power_off(dev);
    psleep(SHUT_DOWN_DELAY_MS);
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
    psleep(1);
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
  // Ask for PRV too until a boot with it fails, then fall back to HRM alone
  if (!s_probe.prv_failed) {
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

  if (dev->led_supply != nullptr) {
    (void)pbl_regulator_enable(dev->led_supply);
  }
  pbl_gpio_set(&dev->enable, true);
  state->power_on_ticks = rtc_get_ticks();
  state->handshake_count = 0;
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
  psleep(BOOT_DELAY_MS);
  const bool found = prv_read(dev, ADDR_INFO_START, &info, sizeof(info));
  pbl_gpio_set(&dev->enable, false);

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
    // No usable application in the sensor flash, and it is never reflashed
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

  (void)features;
  (void)low_latency;

  if (!hrm_is_present(dev)) {
    return false;
  }

  pbl_mutex_lock(&state->lock, PBL_FOREVER);
  if (state->state == AS7000State_Off) {
    if (state->boot_failures >= MAX_BOOT_FAILURES) {
      ret = false;
    } else {
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

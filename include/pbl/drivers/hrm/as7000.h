/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/exti.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/regulator.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/compiler.h>
#include <pbl/kernel/mutex.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/regular_timer.h>

#include <kernel/util/interval_timer.h>

/**
 * @defgroup drivers_hrm_as7000 AS7000
 * @ingroup drivers_hrm
 * @brief @ref drivers_hrm implementation for the ams AS7000 (Pebble 2 HR).
 *
 * Runs the HRM application already in the sensor's flash; only personal builds with
 * CONFIG_HRM_AS7000_FW_UPDATE write the sensor firmware. The sensor reports heart rate only.
 * Whether it is fitted is probed at init.
 * @{
 */

/** @brief Sensor information registers. */
typedef struct PBL_PACKED {
  uint8_t protocol_version_major;
  uint8_t protocol_version_minor;
  uint8_t sw_version_major;
  uint8_t sw_version_minor;
  uint8_t application_id;
  uint8_t hw_revision;
} AS7000InfoRecord;

/** @cond INTERNAL_HIDDEN */
typedef enum {
  AS7000State_Uninitialized,
  AS7000State_Absent,
  AS7000State_Off,
  AS7000State_Booting,
  AS7000State_On,
} AS7000State;

typedef struct HRMDeviceState {
  AS7000State state;
  struct pbl_mutex lock;
  TimerID boot_timer;
  RtcTicks power_on_ticks;
  RtcTicks power_off_ticks;
  uint32_t handshake_count;
  uint8_t boot_failures;
  uint8_t pres_detect_thrsh;
  // HRMFeature bits the sensor was last enabled for
  uint32_t features;
  bool accel_skip_next;
  // Accel samples sent at the last handshake, and whether the sensor said it lacked them
  uint8_t accel_sent;
  bool no_accel;
  volatile bool handshake_pending;
  volatile uint8_t missed_seconds;
  RegularTimerInfo watchdog;
  IntervalTimer handshake_interval;
  AS7000InfoRecord info;
} HRMDeviceState;
/** @endcond */

/** @brief AS7000 device. */
typedef const struct HRMDevice {
  /** Driver runtime state. */
  HRMDeviceState *state;
  /** Sensor I2C device. */
  const struct pbl_i2c_dev *i2c;
  /** Handshake interrupt line. */
  ExtiConfig int_exti;
  /** Handshake line, as an input. */
  struct pbl_gpio int_gpio;
  /** Enable output, active while the sensor runs. */
  struct pbl_gpio enable;
  /** LED supply, optional. */
  const struct pbl_regulator *led_supply;
} HRMDevice;

#ifdef CONFIG_HRM_AS7000_PRV_PROBE
/** @brief Registers dumped by the PRV probe: 0x10-0x13, then 0x20-0x33. */
#define AS7000_PROBE_NUM_REGS 24

/** @brief Handshakes kept in the PRV probe history. */
#define AS7000_PROBE_HISTORY 8
/** @brief Length of the block at 0x2c that changes with PRV running. */
#define AS7000_PROBE_BLOCK_LEN 5

/** @brief One handshake of the PRV probe history. */
typedef struct {
  /** Register 0x12. */
  uint8_t reg12;
  /** Block at 0x2c, read twice back to back. */
  uint8_t first[AS7000_PROBE_BLOCK_LEN];
  uint8_t second[AS7000_PROBE_BLOCK_LEN];
} AS7000ProbeSample;

/** @brief PRV probe snapshot, kept after the sensor turns off. */
typedef struct {
  /** Application IDs written, and read back once the sensor was ready. */
  uint8_t requested;
  uint8_t running;
  /** A boot with PRV requested failed; HRM runs alone since. */
  bool prv_failed;
  /** Successful boots. */
  uint16_t boots;
  /** Handshakes sampled. */
  uint32_t handshakes;
  /** BPM, status and signal quality (254 = off wrist) at the last handshake. */
  uint8_t bpm;
  uint8_t status;
  uint8_t sqi;
  /** PPG samples the sensor took in the last second, and the range seen. */
  uint8_t ppg_samples;
  uint8_t ppg_min;
  uint8_t ppg_max;
  /** Register values at the last handshake. */
  uint8_t regs[AS7000_PROBE_NUM_REGS];
  /** Bit n set once register n changed between handshakes. */
  uint32_t changed;
  /** Last handshakes, oldest first from @ref history_next. */
  AS7000ProbeSample history[AS7000_PROBE_HISTORY];
  uint8_t history_next;
  /** Largest value seen at 0x2c. */
  uint8_t max_2c;
  /** Handshakes where the second read of the 0x2c block differed from the first. */
  uint16_t advances;
} AS7000Probe;

/**
 * @brief Get the PRV probe snapshot.
 *
 * @param dev HRM device.
 * @param[out] probe Snapshot.
 * @return True if the sensor is present.
 */
bool as7000_get_probe(HRMDevice *dev, AS7000Probe *probe);
#endif

#ifdef CONFIG_HRM_AS7000_FW_UPDATE
/** @brief Outcome of the sensor firmware update at init. */
typedef enum {
  AS7000FwUpdateResult_None,
  /** The sensor already runs the image's version. */
  AS7000FwUpdateResult_Current,
  /** Flashed, and the new version runs. */
  AS7000FwUpdateResult_Updated,
  /** The sensor never switched to its loader. Nothing was written. */
  AS7000FwUpdateResult_NoLoader,
  /** The loader did not report ready, or reported an error. */
  AS7000FwUpdateResult_NotReady,
  /** A record could not be written. */
  AS7000FwUpdateResult_WriteFailed,
  /** The image resource is missing or unreadable. */
  AS7000FwUpdateResult_BadImage,
  /** All records were sent; still waiting to verify. */
  AS7000FwUpdateResult_Flashed,
  /** All records were sent, but the new version did not start. */
  AS7000FwUpdateResult_NotRunning,
} AS7000FwUpdateResult;

/** @brief Sensor firmware update record. */
typedef struct {
  AS7000FwUpdateResult result;
  /** Version of the image in the system resources. */
  uint8_t image_major;
  uint8_t image_minor;
  /** Sensor information before the update. */
  AS7000InfoRecord from;
  /** Records the loader accepted. */
  uint16_t records;
  /** Unexpected loader status, if any. */
  uint8_t status;
} AS7000FwUpdate;

/**
 * @brief Get the sensor firmware update record.
 *
 * @param dev HRM device.
 * @param[out] update Record.
 */
void as7000_get_fw_update(HRMDevice *dev, AS7000FwUpdate *update);
#endif

/**
 * @brief Get the sensor information read at init.
 *
 * @param dev HRM device.
 * @param[out] info Sensor information.
 * @return True if the sensor is present.
 */
bool as7000_get_version_info(HRMDevice *dev, AS7000InfoRecord *info);

/** @} */

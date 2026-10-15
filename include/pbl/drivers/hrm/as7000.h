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
 * Runs the HRM application already in the sensor's flash; the driver never writes the sensor
 * firmware. The sensor reports heart rate only. Whether it is fitted is probed at init.
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
  uint32_t handshake_count;
  uint8_t boot_failures;
  uint8_t pres_detect_thrsh;
  bool accel_skip_next;
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
  /** BPM register at the last handshake. */
  uint8_t bpm;
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

/**
 * @brief Get the sensor information read at init.
 *
 * @param dev HRM device.
 * @param[out] info Sensor information.
 * @return True if the sensor is present.
 */
bool as7000_get_version_info(HRMDevice *dev, AS7000InfoRecord *info);

/** @} */

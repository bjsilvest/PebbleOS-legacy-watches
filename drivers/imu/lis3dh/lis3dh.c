/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! ST LIS3DH accelerometer on I2C (the original Pebble), configured as the
//! stock firmware did: +/-4 g, high resolution, the FIFO watermark on INT1
//! and single clicks on any axis, the stock "shake", on INT2.
//!
//! Batches of more than one sample come from the FIFO in stream mode; a
//! batch of one uses the data ready interrupt. With neither data nor shake
//! detection wanted, the chip is powered down.

#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include <pbl/drivers/accel.h>
#include <pbl/drivers/exti.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/imu/lis3dh/lis3dh.h>
#include <pbl/drivers/rtc.h>
#include <pbl/kernel/delay.h>
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/util/math.h>
#include <pbl/util/misc.h>
#include <pbl/util/size.h>

#include <board/board.h>

PBL_LOG_MODULE_DEFINE(driver_accel_lis3dh, CONFIG_DRIVER_IMU_LOG_LEVEL);

static const struct pbl_lis3dh *s_dev;

static void prv_work_cb(void *data) {
  void (*work)(void) = data;

  pbl_accel_lock(&s_dev->accel);
  work();
  pbl_accel_unlock(&s_dev->accel);
}

//! Run driver work on the timer task, with the device lock held
static void prv_queue_work_from_isr(void (*work)(void)) {
  new_timer_add_work_callback_from_isr(prv_work_cb, (void *)work);
}

#define REG_WHO_AM_I    0x0fU
#define REG_CTRL1       0x20U
#define REG_CTRL3       0x22U
#define REG_CTRL4       0x23U
#define REG_CTRL5       0x24U
#define REG_CTRL6       0x25U
#define REG_OUT_X_L     0x28U
#define REG_FIFO_CTRL   0x2eU
#define REG_FIFO_SRC    0x2fU
#define REG_CLICK_CFG   0x38U
#define REG_CLICK_SRC   0x39U
#define REG_CLICK_THS   0x3aU
#define REG_TIME_LIMIT  0x3bU
#define REG_TIME_LATENC 0x3cU
#define REG_TIME_WINDOW 0x3dU

//! Set in the register address to read several registers in one go.
#define AUTO_INCREMENT 0x80U

#define WHO_AM_I_VAL 0x33U

#define CTRL1_XYZ_EN  0x07U
#define CTRL3_I1_DRDY (1U << 4)
#define CTRL3_I1_WTM  (1U << 2)
#define CTRL4_BDU     (1U << 7)
#define CTRL4_FS_4G   (1U << 4)
#define CTRL4_HR      (1U << 3)
#define CTRL5_FIFO_EN (1U << 6)
#define CTRL6_I2_CLICK (1U << 7)

#define FIFO_MODE_BYPASS 0x00U
#define FIFO_MODE_STREAM 0x80U
#define FIFO_SRC_OVRN    (1U << 6)
#define FIFO_SRC_FSS     0x1fU
#define FIFO_DEPTH       32U

#define CLICK_CFG_SINGLE_XYZ 0x15U
#define CLICK_SRC_IA         (1U << 6)
#define CLICK_SRC_SIGN       (1U << 3)
//! Latch the click interrupt until CLICK_SRC is read.
#define CLICK_THS_LIR (1U << 7)

#define SAMPLE_SIZE 6U
//! +/-4 g in high resolution: 2 mg per 12-bit count, left aligned in 16 bits.
#define SCALE_NUM 1
#define SCALE_DEN 8

//! Shake (click) detection on its own runs at 25 Hz, as stock did.
#define SHAKE_ODR_HZ 25U

static const struct {
  uint32_t hz;
  uint8_t odr;
} s_rates[] = {
  {1U, 0x1U}, {10U, 0x2U}, {25U, 0x3U}, {50U, 0x4U}, {100U, 0x5U}, {200U, 0x6U},
};

static bool s_present;
static uint32_t s_data_hz = 25U;
static uint32_t s_num_samples;
static bool s_shake_enabled;
static bool s_rotated_180;
static uint8_t s_shake_threshold;
static volatile bool s_data_work_pending;
static volatile bool s_shake_work_pending;
static uint8_t s_fifo_buf[FIFO_DEPTH * SAMPLE_SIZE];
//! The newest sample delivered from the FIFO, and when.
static uint8_t s_last_raw[SAMPLE_SIZE];
static uint64_t s_last_us;

static bool prv_write(uint8_t reg, uint8_t value) {
  return pbl_i2c_reg_write_byte(I2C_LIS3DH, reg, value) == 0;
}

static bool prv_read(uint8_t reg, uint8_t *data, uint32_t len) {
  if (len > 1U) {
    reg |= AUTO_INCREMENT;
  }
  return pbl_i2c_burst_read(I2C_LIS3DH, reg, data, len) == 0;
}

static uint64_t prv_now_us(void) {
  time_t s;
  uint16_t ms;
  rtc_get_time_ms(&s, &ms);
  return ((uint64_t)s * 1000U + ms) * 1000U;
}

static ExtiConfig prv_exti(int i) {
  return pbl_exti_stm32_config(&BOARD_CONFIG_ACCEL.accel_int_gpios[i]);
}

//! Output data rate: the data rate when samples are wanted, else the shake
//! detection rate, else powered down.
static uint32_t prv_active_hz(void) {
  if (s_num_samples > 0U) {
    return s_data_hz;
  }
  return s_shake_enabled ? SHAKE_ODR_HZ : 0U;
}

static uint8_t prv_odr_bits(uint32_t hz) {
  for (size_t i = 0; i < ARRAY_LENGTH(s_rates); i++) {
    if (s_rates[i].hz == hz) {
      return s_rates[i].odr;
    }
  }
  return 0U;
}

//! The stock click timing: limit, latency and window in output samples.
static void prv_configure_click_timing(uint32_t hz) {
  prv_write(REG_TIME_LIMIT, MIN(hz * 2240U / 1000U, 0x7fU));
  prv_write(REG_TIME_LATENC, MIN(hz * 1280U / 1000U, 0xffU));
  prv_write(REG_TIME_WINDOW, MIN(hz * 5120U / 1000U, 0xffU));
}

static uint8_t prv_shake_threshold(void) {
  const uint8_t threshold =
      s_shake_threshold ?: BOARD_CONFIG_ACCEL.accel_config.shake_thresholds[AccelThresholdHigh];
  return MIN(threshold, 0x7fU);
}

//! Apply the whole configuration. Interrupts are off meanwhile.
static void prv_configure(void) {
  if (!s_present) {
    return;
  }
  exti_disable(prv_exti(0));
  exti_disable(prv_exti(1));

  const uint32_t hz = prv_active_hz();
  const bool fifo = s_num_samples > 1U;

  // Power down while reconfiguring, which also empties the FIFO.
  prv_write(REG_CTRL1, 0x00U);
  prv_write(REG_FIFO_CTRL, FIFO_MODE_BYPASS);
  prv_write(REG_CTRL5, fifo ? CTRL5_FIFO_EN : 0x00U);
  prv_write(REG_CTRL3, fifo ? CTRL3_I1_WTM : ((s_num_samples == 1U) ? CTRL3_I1_DRDY : 0x00U));
  prv_write(REG_CTRL6, s_shake_enabled ? CTRL6_I2_CLICK : 0x00U);
  prv_write(REG_CLICK_CFG, s_shake_enabled ? CLICK_CFG_SINGLE_XYZ : 0x00U);
  prv_write(REG_CLICK_THS, CLICK_THS_LIR | prv_shake_threshold());
  if (hz != 0U) {
    prv_configure_click_timing(hz);
  }
  if (fifo) {
    // The watermark interrupt fires once more than wtm samples are queued.
    prv_write(REG_FIFO_CTRL, FIFO_MODE_STREAM | (uint8_t)(s_num_samples - 1U));
  }
  if (hz == 0U) {
    return;
  }
  prv_write(REG_CTRL1, (uint8_t)(prv_odr_bits(hz) << 4) | CTRL1_XYZ_EN);

  // Clear anything latched, then listen.
  uint8_t scratch[SAMPLE_SIZE];
  prv_read(REG_CLICK_SRC, scratch, 1U);
  prv_read(REG_OUT_X_L, scratch, sizeof(scratch));
  if (s_num_samples > 0U) {
    exti_enable(prv_exti(0));
  }
  if (s_shake_enabled) {
    exti_enable(prv_exti(1));
  }
}

static AccelRawBatch prv_batch(const uint8_t *data, uint16_t num_samples, uint64_t last_us) {
  const AccelConfig *cfg = &BOARD_CONFIG_ACCEL.accel_config;
  const uint32_t interval_us = 1000000U / s_data_hz;
  AccelRawBatch batch = {
    .data = data,
    .num_samples = num_samples,
    .stride = SAMPLE_SIZE,
    .scale_num = SCALE_NUM,
    .scale_den = SCALE_DEN,
    .first_timestamp_us = last_us - (uint64_t)(num_samples - 1U) * interval_us,
    .sampling_interval_us = interval_us,
  };
  for (int axis = 0; axis < 3; axis++) {
    // Rotated by 180 degrees, X and Y flip.
    const bool flip = s_rotated_180 && (axis != AXIS_Z);
    batch.axis[axis].offset = 2U * cfg->axes_offsets[axis];
    batch.axis[axis].sign = (cfg->axes_inverts[axis] != flip) ? -1 : 1;
  }
  return batch;
}

static void prv_handle_data(void) {
  s_data_work_pending = false;
  if (s_num_samples == 0U) {
    return;
  }

  uint16_t count = 1U;
  if (s_num_samples > 1U) {
    uint8_t src = 0U;
    if (!prv_read(REG_FIFO_SRC, &src, 1U)) {
      return;
    }
    count = (src & FIFO_SRC_OVRN) ? FIFO_DEPTH : (src & FIFO_SRC_FSS);
    if (src & FIFO_SRC_OVRN) {
      PBL_LOG_WRN("LIS3DH FIFO overrun");
    }
    if (count == 0U) {
      return;
    }
  }
  const uint64_t now_us = prv_now_us();
  // In FIFO mode the address wraps back to OUT_X_L after each sample.
  if (!prv_read(REG_OUT_X_L, s_fifo_buf, count * SAMPLE_SIZE)) {
    return;
  }
  memcpy(s_last_raw, &s_fifo_buf[(count - 1U) * SAMPLE_SIZE], SAMPLE_SIZE);
  s_last_us = now_us;
  const AccelRawBatch batch = prv_batch(s_fifo_buf, count, now_us);
  pbl_accel_report_samples(&s_dev->accel, &batch);
}

static void prv_handle_shake(void) {
  s_shake_work_pending = false;
  uint8_t src = 0U;
  if (!s_shake_enabled || !prv_read(REG_CLICK_SRC, &src, 1U) || !(src & CLICK_SRC_IA)) {
    return;
  }
  // Chip axes are bits 0-2 (X, Y, Z); report the first in watch axes.
  const AccelConfig *cfg = &BOARD_CONFIG_ACCEL.accel_config;
  for (int axis = 0; axis < 3; axis++) {
    if (src & (1U << cfg->axes_offsets[axis])) {
      int32_t direction = (src & CLICK_SRC_SIGN) ? -1 : 1;
      if (cfg->axes_inverts[axis] != (s_rotated_180 && axis != AXIS_Z)) {
        direction = -direction;
      }
      pbl_accel_report_event(&s_dev->accel, &(const struct pbl_accel_event){
                                                .type = PBL_ACCEL_EVENT_SHAKE,
                                                .axis = axis,
                                                .direction = direction,
                                              });
      return;
    }
  }
}

static void prv_int1_handler(void) {
  if (!s_data_work_pending) {
    s_data_work_pending = true;
    prv_queue_work_from_isr(prv_handle_data);
  }
}

static void prv_int2_handler(void) {
  if (!s_shake_work_pending) {
    s_shake_work_pending = true;
    prv_queue_work_from_isr(prv_handle_shake);
  }
}

static int prv_init(const struct pbl_accel *accel) {
  uint8_t whoami = 0U;

  s_dev = container_of(accel, const struct pbl_lis3dh, accel);
  prv_read(REG_WHO_AM_I, &whoami, 1U);
  s_present = (whoami == WHO_AM_I_VAL);
  if (!s_present) {
    PBL_LOG_ERR("LIS3DH whoami 0x%x, expecting 0x%x", whoami, WHO_AM_I_VAL);
    return -ENODEV;
  }

  // Block data update, +/-4 g, high resolution; interrupts active high.
  prv_write(REG_CTRL4, CTRL4_BDU | CTRL4_FS_4G | CTRL4_HR);
  for (int i = 0; i < 2; i++) {
    pbl_gpio_configure(&BOARD_CONFIG_ACCEL.accel_int_gpios[i], PBL_GPIO_INPUT);
  }
  exti_configure_pin(prv_exti(0), ExtiTrigger_Rising, prv_int1_handler);
  exti_configure_pin(prv_exti(1), ExtiTrigger_Rising, prv_int2_handler);
  prv_configure();

  return 0;
}

static uint32_t prv_get_sampling_interval(const struct pbl_accel *accel);

static int prv_set_sampling_interval(const struct pbl_accel *accel, uint32_t interval_us,
                                     uint32_t *actual_us) {
  // The longest interval no longer than asked for, else the shortest.
  uint32_t hz = s_rates[ARRAY_LENGTH(s_rates) - 1].hz;
  for (size_t i = 0; i < ARRAY_LENGTH(s_rates); i++) {
    if (1000000U / s_rates[i].hz <= interval_us) {
      hz = s_rates[i].hz;
      break;
    }
  }
  if (hz != s_data_hz) {
    if (s_num_samples > 1U) {
      prv_handle_data(); // flush with the old interval's timestamps
    }
    s_data_hz = hz;
    prv_configure();
  }
  *actual_us = prv_get_sampling_interval(accel);
  return 0;
}

static uint32_t prv_get_sampling_interval(const struct pbl_accel *accel) {
  return 1000000U / s_data_hz;
}

static uint32_t prv_get_max_num_samples(const struct pbl_accel *accel) {
  return FIFO_DEPTH;
}

static int prv_set_num_samples(const struct pbl_accel *accel, uint32_t num_samples) {
  num_samples = MIN(num_samples, FIFO_DEPTH);
  if (num_samples == s_num_samples) {
    return 0;
  }
  if (s_num_samples > 1U) {
    prv_handle_data(); // flush what is queued
  }
  s_num_samples = num_samples;
  prv_configure();
  return 0;
}

//! Read the output registers, powering the chip up for one sample if off.
static bool prv_peek_raw(uint8_t raw[SAMPLE_SIZE]) {
  const bool powered = prv_active_hz() != 0U;
  if (!powered) {
    // One sample at 100 Hz: 1 ms to start, then 10 ms per sample.
    prv_write(REG_CTRL1, (uint8_t)(prv_odr_bits(100U) << 4) | CTRL1_XYZ_EN);
    pbl_sleep_ms(12);
  }
  const bool ok = prv_read(REG_OUT_X_L, raw, SAMPLE_SIZE);
  if (!powered) {
    prv_write(REG_CTRL1, 0x00U);
  }
  return ok;
}

static int prv_peek(const struct pbl_accel *accel, AccelDriverSample *data) {
  if (!s_present) {
    return -ENODEV;
  }
  uint8_t raw[SAMPLE_SIZE];
  uint64_t when_us;
  if (s_num_samples > 1U) {
    // Reading the outputs would pop the FIFO: deliver what is queued and
    // report the newest sample.
    prv_handle_data();
    memcpy(raw, s_last_raw, sizeof(raw));
    when_us = s_last_us;
  } else if (!prv_peek_raw(raw)) {
    return -EIO;
  } else {
    when_us = prv_now_us();
  }

  const AccelRawBatch batch = prv_batch(raw, 1U, when_us);
  int16_t value[3];
  for (int axis = 0; axis < 3; axis++) {
    const uint8_t *p = &raw[batch.axis[axis].offset];
    const int16_t count = (int16_t)(p[0] | (p[1] << 8));
    value[axis] = (int16_t)(batch.axis[axis].sign * count * SCALE_NUM / SCALE_DEN);
  }
  *data = (AccelDriverSample){
    .timestamp_us = batch.first_timestamp_us,
    .x = value[AXIS_X],
    .y = value[AXIS_Y],
    .z = value[AXIS_Z],
  };
  return 0;
}

static int prv_set_shake_detection(const struct pbl_accel *accel, bool on) {
  if (on != s_shake_enabled) {
    s_shake_enabled = on;
    prv_configure();
  }
  return 0;
}

static int prv_set_shake_sensitivity_high(const struct pbl_accel *accel, bool sensitivity_high) {
  const AccelConfig *cfg = &BOARD_CONFIG_ACCEL.accel_config;
  const uint8_t threshold = sensitivity_high ? cfg->shake_thresholds[AccelThresholdLow]
                                             : prv_shake_threshold();
  if (s_present) {
    prv_write(REG_CLICK_THS, CLICK_THS_LIR | MIN(threshold, 0x7fU));
  }
  return 0;
}

static int prv_set_shake_sensitivity_percent(const struct pbl_accel *accel, uint8_t percent) {
  // Higher is more sensitive. The settings default (55, "Medium") keeps the
  // stock threshold, 100 is the stock high sensitivity one.
  const int medium = 55;
  const int normal = BOARD_CONFIG_ACCEL.accel_config.shake_thresholds[AccelThresholdHigh];
  const int sensitive = BOARD_CONFIG_ACCEL.accel_config.shake_thresholds[AccelThresholdLow];
  const int p = MIN(percent, 100);

  int threshold;
  if (p >= medium) {
    threshold = normal - ((normal - sensitive) * (p - medium)) / (100 - medium);
  } else {
    threshold = normal + (normal * (medium - p)) / medium;
  }
  s_shake_threshold = MIN(threshold, 0x7f);
  if (s_present) {
    prv_write(REG_CLICK_THS, CLICK_THS_LIR | s_shake_threshold);
  }
  return 0;
}

static int prv_set_rotated(const struct pbl_accel *accel, bool rotated) {
  s_rotated_180 = rotated;
  return 0;
}

const struct pbl_accel_ops pbl_lis3dh_ops = {
  .init = prv_init,
  .set_rotated = prv_set_rotated,
  .set_sampling_interval = prv_set_sampling_interval,
  .get_sampling_interval = prv_get_sampling_interval,
  .set_num_samples = prv_set_num_samples,
  .get_max_num_samples = prv_get_max_num_samples,
  .peek = prv_peek,
  .set_shake_detection = prv_set_shake_detection,
  .set_shake_sensitivity_high = prv_set_shake_sensitivity_high,
  .set_shake_sensitivity_percent = prv_set_shake_sensitivity_percent,
};

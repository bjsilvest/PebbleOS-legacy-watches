/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! NXP MAG3110 magnetometer on I2C (snowy, tintin), as the stock firmware
//! drove it: raw mode with automatic sensor reset, 5 or 20 Hz, an interrupt
//! per sample. An optional switched supply is on only while sampling.

#include <errno.h>
#include <stdint.h>

#include <pbl/drivers/exti.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/imu/mag3110/mag3110.h>
#include <pbl/drivers/mag.h>
#include <pbl/drivers/regulator.h>
#include <pbl/kernel/delay.h>
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/util/misc.h>

#include <board/board.h>

PBL_LOG_MODULE_DEFINE(driver_mag, CONFIG_DRIVER_IMU_LOG_LEVEL);

#define DR_STATUS_REG 0x00
#define OUT_X_MSB_REG 0x01 // a 6-byte read here returns X, Y, Z
#define WHO_AM_I_REG  0x07
#define SYSMOD_REG    0x08
#define CTRL_REG1     0x10
#define CTRL_REG2     0x11

#define MAG3110_WHOAMI 0xc4

// Data rate bits of CTRL_REG1 (datasheet table 25), oversampling 0
#define CTRL_REG1_ODR_20HZ (0x1 << 6)
#define CTRL_REG1_ODR_5HZ  (0x2 << 6)
#define CTRL_REG1_ACTIVE   0x01
// Automatic magnetic sensor reset, raw mode
#define CTRL_REG2_AUTO_RESET_RAW 0xA0

static const struct pbl_mag3110 *s_dev;

static int prv_read(uint8_t reg, uint8_t len, uint8_t *data) {
  int ret;

  pbl_i2c_use(&s_dev->i2c);
  ret = pbl_i2c_read_register_block(&s_dev->i2c, reg, len, data);
  pbl_i2c_release(&s_dev->i2c);

  return ret;
}

static int prv_write(uint8_t reg, uint8_t data) {
  int ret;

  pbl_i2c_use(&s_dev->i2c);
  ret = pbl_i2c_write_register_block(&s_dev->i2c, reg, 1, &data);
  pbl_i2c_release(&s_dev->i2c);

  return ret;
}

static void prv_supply(bool on) {
  if (s_dev->supply == nullptr) {
    return;
  }

  if (on) {
    (void)pbl_regulator_enable(s_dev->supply);
    // Supply up and the chip out of its 1.7 ms power-on reset.
    pbl_sleep_ms(10);
  } else {
    (void)pbl_regulator_disable(s_dev->supply);
  }
}

static ExtiConfig prv_exti(void) {
  return pbl_exti_stm32_config(&s_dev->int_gpio);
}

//! Standby: not sampling and no interrupts. Takes up to 1/ODR (200 ms at
//! 5 Hz) to take effect. A sample finished meanwhile is read out: its data
//! ready interrupt would otherwise stay high, and the rising edge EXTI would
//! miss every sample after it.
static int prv_enter_standby_mode(void) {
  int ret;

  ret = prv_write(CTRL_REG1, 0x00);
  if (ret != 0) {
    return ret;
  }

  for (int i = 0; i < 150; ++i) {
    uint8_t sysmod = 0;
    ret = prv_read(SYSMOD_REG, 1, &sysmod);
    if (ret != 0) {
      return ret;
    }
    if (sysmod == 0) {
      uint8_t raw_data[7];
      return prv_read(DR_STATUS_REG, sizeof(raw_data), raw_data);
    }
    pbl_sleep_ms(2);
  }

  return -ETIMEDOUT;
}

//! Align a big-endian axis reading with the watch's coordinate system.
static int16_t prv_align_axis(int axis, const uint8_t *raw_data) {
  const int offset = 2 * s_dev->axis_map[axis];
  const int16_t value = (int16_t)((raw_data[offset] << 8) | raw_data[offset + 1]);
  return (int16_t)(s_dev->axis_dir[axis] * value);
}

//! Read a sample; reading clears the data ready interrupt.
static int prv_read_sample(MagData *sample) {
  uint8_t raw_data[7];
  int ret;

  // DR_STATUS_REG is right before the data registers.
  ret = prv_read(DR_STATUS_REG, sizeof(raw_data), raw_data);
  if (ret != 0) {
    return ret;
  }

  const int rotate = s_dev->state->rotated ? -1 : 1;
  sample->x = (int16_t)(rotate * prv_align_axis(0, &raw_data[1]));
  sample->y = (int16_t)(rotate * prv_align_axis(1, &raw_data[1]));
  sample->z = prv_align_axis(2, &raw_data[1]);

  return 0;
}

static void prv_sample_work_cb(void *data) {
  MagData sample;

  pbl_mag_lock(&s_dev->mag);

  // Spurious: fired after sampling was turned off.
  if (s_dev->state->rate_hz != 0U && prv_read_sample(&sample) == 0) {
    pbl_mag_report_sample(&s_dev->mag, &sample);
  }

  pbl_mag_unlock(&s_dev->mag);
}

static void prv_interrupt_handler(void) {
  new_timer_add_work_callback_from_isr(prv_sample_work_cb, nullptr);
}

static int prv_init(const struct pbl_mag *mag) {
  uint8_t whoami = 0;

  s_dev = container_of(mag, const struct pbl_mag3110, mag);

  prv_supply(true);
  const int ret = prv_read(WHO_AM_I_REG, 1, &whoami);
  prv_supply(false);
  if (ret != 0 || whoami != MAG3110_WHOAMI) {
    PBL_LOG_WRN("MAG3110 whoami 0x%x, expecting 0x%x", whoami, MAG3110_WHOAMI);
    return -ENODEV;
  }

  (void)pbl_gpio_configure(&s_dev->int_gpio, PBL_GPIO_INPUT);
  exti_configure_pin(prv_exti(), ExtiTrigger_Rising, prv_interrupt_handler);

  return 0;
}

static int prv_set_rotated(const struct pbl_mag *mag, bool rotated) {
  s_dev->state->rotated = rotated;

  return 0;
}

static int prv_set_sampling_interval(const struct pbl_mag *mag, uint32_t interval_us,
                                     uint32_t *actual_us) {
  struct pbl_mag3110_state *state = s_dev->state;
  uint8_t rate_hz = 0U;
  int ret;

  if (interval_us > 0U) {
    rate_hz = (interval_us <= 1000000U / 20U) ? 20U : 5U;
  }

  if (rate_hz != state->rate_hz) {
    PBL_LOG_DBG("Setting sample rate to %u Hz", rate_hz);

    if (state->rate_hz == 0U) {
      prv_supply(true);
    }

    // The rate can only change in standby.
    ret = prv_enter_standby_mode();
    if (ret != 0) {
      return ret;
    }

    if (rate_hz == 0U) {
      exti_disable(prv_exti());
      prv_supply(false);
    } else {
      if (state->rate_hz == 0U) {
        ret = prv_write(CTRL_REG2, CTRL_REG2_AUTO_RESET_RAW);
        if (ret != 0) {
          return ret;
        }
        exti_enable(prv_exti());
      }

      ret = prv_write(CTRL_REG1, ((rate_hz == 20U) ? CTRL_REG1_ODR_20HZ : CTRL_REG1_ODR_5HZ) |
                                     CTRL_REG1_ACTIVE);
      if (ret != 0) {
        return ret;
      }
    }

    state->rate_hz = rate_hz;
  }

  *actual_us = (rate_hz == 0U) ? 0U : 1000000U / rate_hz;

  return 0;
}

static int prv_peek(const struct pbl_mag *mag, MagData *sample) {
  if (s_dev->state->rate_hz == 0U) {
    return -ENODATA;
  }

  return prv_read_sample(sample);
}

const struct pbl_mag_ops pbl_mag3110_ops = {
  .init = prv_init,
  .set_rotated = prv_set_rotated,
  .set_sampling_interval = prv_set_sampling_interval,
  .peek = prv_peek,
};

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! NXP MAG3110 magnetometer on I2C (snowy), as the stock firmware drove it:
//! raw mode with automatic sensor reset, 5 or 20 Hz, an interrupt per sample.

#include <inttypes.h>
#include <stdint.h>

#include <pbl/drivers/exti.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/mag.h>
#include <pbl/kernel/mutex.h>
#include <pbl/logging/logging.h>

#include <board/board.h>
#include <kernel/events.h>
#include <kernel/util/sleep.h>
#include <system/passert.h>

PBL_LOG_MODULE_DEFINE(driver_mag, CONFIG_DRIVER_IMU_LOG_LEVEL);

#define DR_STATUS_REG 0x00
#define OUT_X_MSB_REG 0x01 // a 6-byte read here returns X, Y, Z
#define WHO_AM_I_REG  0x07
#define SYSMOD_REG    0x08
#define CTRL_REG1     0x10
#define CTRL_REG2     0x11

#define MAG3110_WHOAMI 0xc4

static PBL_MUTEX_DEFINE(s_mag_mutex);
static bool s_initialized;
static bool s_present;
static int s_use_refcount;

static bool prv_read(uint8_t reg, uint8_t len, uint8_t *data) {
  pbl_i2c_use(I2C_MAG3110);
  const bool ok = pbl_i2c_read_register_block(I2C_MAG3110, reg, len, data) == 0;
  pbl_i2c_release(I2C_MAG3110);
  return ok;
}

static bool prv_write(uint8_t reg, uint8_t data) {
  pbl_i2c_use(I2C_MAG3110);
  const bool ok = pbl_i2c_write_register_block(I2C_MAG3110, reg, 1, &data) == 0;
  pbl_i2c_release(I2C_MAG3110);
  return ok;
}

static ExtiConfig prv_exti(void) {
  return pbl_exti_stm32_config(&BOARD_MAG_INT);
}

static void prv_interrupt_handler(void) {
  if (s_use_refcount == 0) {
    // Spurious: fired after the mag was turned off.
    return;
  }

  PebbleEvent e = {
    .type = PEBBLE_ECOMPASS_SERVICE_EVENT,
  };
  event_put_isr(&e);
}

//! Standby: not sampling and no interrupts. Takes up to 1/ODR (200 ms at
//! 5 Hz) to take effect.
static bool prv_enter_standby_mode(void) {
  if (!prv_write(CTRL_REG1, 0x00)) {
    return false;
  }

  for (int i = 0; i < 150; ++i) {
    uint8_t sysmod = 0;
    if (!prv_read(SYSMOD_REG, 1, &sysmod)) {
      return false;
    }
    if (sysmod == 0) {
      return true;
    }
    psleep(2);
  }
  return false;
}

void mag_init(void) {
  if (s_initialized) {
    return;
  }
  s_initialized = true;

  uint8_t whoami = 0;
  prv_read(WHO_AM_I_REG, 1, &whoami);
  s_present = (whoami == MAG3110_WHOAMI);
  if (!s_present) {
    PBL_LOG_WRN("MAG3110 whoami 0x%x, expecting 0x%x", whoami, MAG3110_WHOAMI);
  }

  pbl_gpio_configure(&BOARD_MAG_INT, PBL_GPIO_INPUT);
  exti_configure_pin(prv_exti(), ExtiTrigger_Rising, prv_interrupt_handler);
}

void mag_use(void) {
  PBL_ASSERTN(s_initialized);

  pbl_mutex_lock(&s_mag_mutex, PBL_FOREVER);
  if (s_use_refcount == 0 && s_present) {
    exti_enable(prv_exti());
  }
  ++s_use_refcount;
  pbl_mutex_unlock(&s_mag_mutex);
}

void mag_release(void) {
  PBL_ASSERTN(s_initialized && s_use_refcount != 0);

  pbl_mutex_lock(&s_mag_mutex, PBL_FOREVER);
  --s_use_refcount;
  if (s_use_refcount == 0 && s_present) {
    // Standby, then read the data to clear the interrupt for next time.
    prv_enter_standby_mode();
    uint8_t raw_data[7];
    prv_read(DR_STATUS_REG, sizeof(raw_data), raw_data);
    exti_disable(prv_exti());
  }
  pbl_mutex_unlock(&s_mag_mutex);
}

//! Align a big-endian axis reading with the watch's coordinate system.
static int16_t prv_align_axis(int axis, const uint8_t *raw_data) {
  const int offset = 2 * BOARD_CONFIG_MAG.mag_config.axes_offsets[axis];
  const bool invert = BOARD_CONFIG_MAG.mag_config.axes_inverts[axis];
  const int16_t value = (int16_t)((raw_data[offset] << 8) | raw_data[offset + 1]);
  return invert ? -value : value;
}

MagReadStatus mag_read_data(MagData *data) {
  if (!s_present) {
    return MagReadNoMag;
  }

  pbl_mutex_lock(&s_mag_mutex, PBL_FOREVER);
  if (s_use_refcount == 0) {
    pbl_mutex_unlock(&s_mag_mutex);
    return MagReadMagOff;
  }

  MagReadStatus rv = MagReadSuccess;
  uint8_t raw_data[7];
  // DR_STATUS_REG is right before the data registers.
  if (!prv_read(DR_STATUS_REG, sizeof(raw_data), raw_data)) {
    rv = MagReadCommunicationFail;
    goto done;
  }

  if ((raw_data[0] & 0xf0) != 0) {
    // Still read on: reading clears the interrupt.
    rv = MagReadClobbered;
  }

  data->x = prv_align_axis(0, &raw_data[1]);
  data->y = prv_align_axis(1, &raw_data[1]);
  data->z = prv_align_axis(2, &raw_data[1]);

done:
  pbl_mutex_unlock(&s_mag_mutex);
  return rv;
}

bool mag_change_sample_rate(MagSampleRate rate) {
  pbl_mutex_lock(&s_mag_mutex, PBL_FOREVER);

  if (s_use_refcount == 0 || !s_present) {
    pbl_mutex_unlock(&s_mag_mutex);
    return true;
  }

  bool success = false;
  // The rate can only change in standby.
  if (!prv_enter_standby_mode()) {
    goto done;
  }

  // Data rate bits of CTRL_REG1 (datasheet table 25), oversampling 0.
  uint8_t odr = 0;
  switch (rate) {
    case MagSampleRate20Hz:
      odr = 0x1 << 6;
      break;
    case MagSampleRate5Hz:
      odr = 0x2 << 6;
      break;
  }

  // The bottom bit makes it active.
  success = prv_write(CTRL_REG1, odr | 0x01);

done:
  pbl_mutex_unlock(&s_mag_mutex);
  return success;
}

void mag_start_sampling(void) {
  mag_use();

  if (s_present) {
    // Automatic magnetic sensor reset, raw mode.
    prv_write(CTRL_REG2, 0xA0);
    mag_change_sample_rate(MagSampleRate5Hz);
  }
}

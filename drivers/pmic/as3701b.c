/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! ams AS3701B PMIC on silk. Register values are the stock firmware's.

#include <errno.h>

#include <pbl/drivers/adc/stm32.h>
#include <pbl/drivers/battery.h>
#include <pbl/drivers/exti.h>
#include <pbl/drivers/pmic.h>
#include <pbl/drivers/pmic/as3701b.h>
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/system_task.h>

#include <board/board.h>
#include <kernel/events.h>
#include <system/passert.h>

PBL_LOG_MODULE_DEFINE(driver_pmic, CONFIG_DRIVER_PMIC_LOG_LEVEL);

#define CHARGER_DEBOUNCE_MS 400

#define AS3701B_CHIP_ID 0x11

typedef enum PmicGpio {
  PmicGpio1 = (1 << 0),
  PmicGpio2 = (1 << 1),
  PmicGpio3 = (1 << 2),
  PmicGpio4 = (1 << 3),
  PmicGpio5 = (1 << 4),
} PmicGpio;

typedef enum {
  PmicRegisters_SD1_VOLTAGE = 0x01,
  PmicRegisters_GPIO2_CNTL = 0x0a,
  PmicRegisters_GPIO5_CNTL = 0x0d,
  PmicRegisters_GPIO_SIG_OUT = 0x20,
  PmicRegisters_REFERENCE_CNTL = 0x35,
  PmicRegisters_RESET_CNTL = 0x36,
  PmicRegisters_REG_STANDBY_MOD1 = 0x39,
  PmicRegisters_PWM_CNTL_L = 0x41,
  PmicRegisters_PWM_CNTL_H = 0x42,
  PmicRegisters_INT_MASK_1 = 0x74,
  PmicRegisters_INT_MASK_2 = 0x75,
  PmicRegisters_INT_STATUS_1 = 0x77,
  PmicRegisters_INT_STATUS_2 = 0x78,
  PmicRegisters_CHARGE_CNTL = 0x80,
  PmicRegisters_CHARGE_VOLTAGE_CNTL = 0x81,
  PmicRegisters_CHARGE_CURRENT_CNTL = 0x82,
  PmicRegisters_CHARGE_CONFIG_1 = 0x83,
  PmicRegisters_CHARGE_CONFIG_2 = 0x84,
  PmicRegisters_CHARGE_STATUS_1 = 0x86,
  PmicRegisters_CHARGE_STATUS_2 = 0x87,
  PmicRegisters_CHIP_ID = 0x90,
  PmicRegisters_CHIP_REV = 0x91,
} PmicRegisters;

//! Interrupt masks for InterruptStatus1 and InterruptMask1
enum PmicInt1 {
  PmicInt1_Trickle = (1 << 0), //!< Trickle charge
  PmicInt1_NoBat = (1 << 1),   //!< Battery detached
  PmicInt1_Resume = (1 << 2),  //!< Resuming charge on drop after full
  PmicInt1_EOC = (1 << 3),     //!< End of charge
  PmicInt1_ChDet = (1 << 4),   //!< Charger detected
  PmicInt1_OnKey = (1 << 5),   //!< On Key held
  PmicInt1_OvTemp = (1 << 6),  //!< Set when 110deg is exceeded
  PmicInt1_LowBat = (1 << 7),  //!< BSUP dropped below ResVoltFall
};

static TimerID s_debounce_charger_timer = TIMER_INVALID_ID;

//! GPIO output states, so single outputs change without a read-modify-write.
static uint8_t s_pmic_gpio_output_state;

static uint8_t s_last_reset_reason;

//! False if the PMIC did not answer at init (e.g. under emulation).
static bool s_present;

//! Reported when the battery cannot be measured. A made-up low value would
//! make the OS try (and fail) to enter standby through the absent PMIC; the
//! cell is still protected by the PMIC's own cutoff and the battery's.
#define BATTERY_NOMINAL_MV 3900

static bool prv_read_register(uint8_t reg, uint8_t *result) {
  pbl_i2c_use(AS3701B->i2c);
  const int ret = pbl_i2c_read_register(AS3701B->i2c, reg, result);
  pbl_i2c_release(AS3701B->i2c);
  return ret == 0;
}

static bool prv_write_register(uint8_t reg, uint8_t value) {
  pbl_i2c_use(AS3701B->i2c);
  const int ret = pbl_i2c_write_register(AS3701B->i2c, reg, value);
  pbl_i2c_release(AS3701B->i2c);
  return ret == 0;
}

static bool prv_set_register_bit(uint8_t reg, uint8_t bit, bool enable) {
  uint8_t val;
  if (!prv_read_register(reg, &val)) {
    return false;
  }
  if (enable) {
    val |= (1 << bit);
  } else {
    val &= ~(1 << bit);
  }
  return prv_write_register(reg, val);
}

static bool prv_set_pmic_gpio_outputs(PmicGpio set_mask, PmicGpio clear_mask) {
  PBL_ASSERTN((set_mask & clear_mask) == 0);
  uint8_t new_output_state = s_pmic_gpio_output_state;
  new_output_state |= set_mask;
  new_output_state &= ~clear_mask;
  if (prv_write_register(PmicRegisters_GPIO_SIG_OUT, new_output_state)) {
    s_pmic_gpio_output_state = new_output_state;
    return true;
  }
  return false;
}

static void prv_handle_charge_state_change(void *null) {
  const bool is_charging = pmic_is_charging();
  const bool is_connected = pmic_is_usb_connected();
  PBL_LOG_INFO("AS3701b Interrupt: Charging? %s Plugged? %s", is_charging ? "YES" : "NO",
               is_connected ? "YES" : "NO");

  PebbleEvent event = {
    .type = PEBBLE_BATTERY_CONNECTION_EVENT,
    .battery_connection = {
      .is_connected = battery_is_usb_connected(),
    },
  };
  event_put(&event);
}

//! Reading the interrupt status registers clears them.
static void prv_clear_pending_interrupts(void) {
  uint8_t throwaway;
  prv_read_register(PmicRegisters_INT_STATUS_1, &throwaway);
  prv_read_register(PmicRegisters_INT_STATUS_2, &throwaway);
}

static void prv_pmic_state_change_cb(void *null) {
  prv_clear_pending_interrupts();
  new_timer_start(s_debounce_charger_timer, CHARGER_DEBOUNCE_MS, prv_handle_charge_state_change,
                  nullptr, 0);
}

static void prv_as3701b_interrupt_handler(void) {
  system_task_add_callback_from_isr(prv_pmic_state_change_cb, nullptr);
}

static void prv_configure_interrupts(void) {
  // Clear pending interrupts in case we were woken from standby
  prv_clear_pending_interrupts();

  const ExtiConfig exti = pbl_exti_stm32_config(&AS3701B->int_gpio);
  exti_configure_pin(exti, ExtiTrigger_Falling, prv_as3701b_interrupt_handler);
  exti_enable(exti);

  const uint8_t mask = (uint8_t)~(PmicInt1_LowBat | PmicInt1_ChDet | PmicInt1_EOC);
  prv_write_register(PmicRegisters_INT_MASK_1, mask);
  prv_write_register(PmicRegisters_INT_MASK_2, ~0);
}

//! 160 Hz clock on PMIC GPIO2, the display VCOM: divisor 16 and 195 us
//! high/low, 1000000 / (16 * 195 * 2) = ~160 Hz.
static void prv_start_160hz_clock(void) {
  const uint8_t pwm_high_low_time_us = (195 - 1);
  prv_write_register(PmicRegisters_PWM_CNTL_H, pwm_high_low_time_us);
  prv_write_register(PmicRegisters_PWM_CNTL_L, pwm_high_low_time_us);

  bool success = false;
  uint8_t ref_cntl;
  if (prv_read_register(PmicRegisters_REFERENCE_CNTL, &ref_cntl)) {
    ref_cntl |= 0x3; // Divisor of 16
    prv_write_register(PmicRegisters_REFERENCE_CNTL, ref_cntl);

    // GPIO2: mode output (bits 6-4 = 1), iosf PWM (bits 3-0 = 0xe)
    const uint8_t val = (1 << 4) | 0x0e;
    success = prv_write_register(PmicRegisters_GPIO2_CNTL, val);
  }
  PBL_ASSERT(success, "Failed to start PMIC 160Hz PWM");
}

static void prv_configure_charging(void) {
  // Low current range, constant current 118 mA.
  bool success = false;
  if (prv_set_register_bit(PmicRegisters_CHARGE_CNTL, 7, true)) {
    uint8_t cntl;
    if (prv_read_register(PmicRegisters_CHARGE_CURRENT_CNTL, &cntl)) {
      cntl = (cntl & 0xf0) | 0x09; // 118mA when cc_range_select = 1
      success = prv_write_register(PmicRegisters_CHARGE_CURRENT_CNTL, cntl);
    }
  }
  if (!success) {
    PBL_LOG_ERR("Could not set pmic charge current.");
  }

  // EOC current: 5% of constant current
  prv_set_register_bit(PmicRegisters_CHARGE_CONFIG_2, 5, false);

  // EOC 4.30V (3.82V + 0.02V * 0x18), Vsup_min kept at 4.20V
  prv_write_register(PmicRegisters_CHARGE_VOLTAGE_CNTL, 0x18 | (1 << 6));

  pmic_set_charger_state(true);

  // AutoResume: resume charging on a voltage drop after EOC
  prv_set_register_bit(PmicRegisters_CHARGE_CNTL, 6, true);
}

static void prv_configure_battery_measure(void) {
  // PMIC GPIO5 (battery measure enable): open-drain, no pull, inverted. 1
  // drives it low (measure), 0 lets it float.
  const bool success = prv_write_register(PmicRegisters_GPIO5_CNTL, 0b10100000) &&
                       prv_set_pmic_gpio_outputs(0, PmicGpio5);
  if (!success) {
    PBL_LOG_ERR("Could not configure the battery measure control GPIO");
  }
}

static bool prv_is_alive(void) {
  uint8_t chip_id;
  if (!prv_read_register(PmicRegisters_CHIP_ID, &chip_id)) {
    return false;
  }
  if (chip_id != AS3701B_CHIP_ID) {
    PBL_LOG_ERR("AS3701B whoami 0x%x, expecting 0x%x", chip_id, AS3701B_CHIP_ID);
    return false;
  }
  return true;
}

//! The STM32F4 may hit a power down reset at 1.76V and the rail has a 3%
//! tolerance, so raise it to 1.825V for a 1.77V minimum.
static void prv_set_sd1_voltage(void) {
  uint8_t sd1_vsel;
  if (prv_read_register(PmicRegisters_SD1_VOLTAGE, &sd1_vsel)) {
    const uint8_t sd1_vsel_mask = 0x3f;
    sd1_vsel &= ~sd1_vsel_mask;
    // V_SD1 = 1.4V + (0x51 - 0x40) * 25mV = 1.825V
    sd1_vsel |= (0x51 & sd1_vsel_mask);
    prv_write_register(PmicRegisters_SD1_VOLTAGE, sd1_vsel);
  }
}

static void prv_stash_last_reset_reason(void) {
  uint8_t reset_cntl;
  if (!prv_read_register(PmicRegisters_RESET_CNTL, &reset_cntl)) {
    PBL_LOG_ERR("Failed to read the RESET_CNTL register");
    return;
  }

  s_last_reset_reason = reset_cntl >> 4;
}

uint32_t pmic_get_last_reset_reason(void) {
  return s_last_reset_reason;
}

int pbl_as3701b_init(const struct pbl_device *dev) {
  s_debounce_charger_timer = new_timer_create();

  pbl_gpio_configure(&AS3701B->int_gpio, PBL_GPIO_INPUT | PBL_GPIO_PULL_UP);

  if (!prv_is_alive()) {
    PBL_LOG_ERR("AS3701B not found; battery state unknown");
    return -ENODEV;
  }
  s_present = true;

  prv_stash_last_reset_reason();

  // Sync the GPIO output register with what we think it holds.
  if (!prv_set_pmic_gpio_outputs(0, 0)) {
    PBL_LOG_ERR("Could not initialize PMIC GPIO outputs");
  }

  prv_set_sd1_voltage();
  prv_start_160hz_clock();
  prv_configure_battery_measure();
  prv_configure_interrupts();
  prv_configure_charging();

  // Override the OTP 'onkey_lpress_reset': shut down rather than reset on a
  // long button hold.
  prv_set_register_bit(PmicRegisters_REFERENCE_CNTL, 5, false);

  return 0;
}

//! A full power off would cut every rail and lose the RTC; enter standby
//! instead, keeping LDO2 (the backup domain) on.
bool pmic_power_off(void) {
  // Only wake on charger detect.
  const uint8_t int_mask = (uint8_t)~(PmicInt1_ChDet);
  prv_write_register(PmicRegisters_INT_MASK_1, int_mask);
  prv_write_register(PmicRegisters_INT_MASK_2, 0xff);

  prv_clear_pending_interrupts();

  // Reg_Standby_mod1: SD1 and LDO1 off, LDO2 on, regulator pulldowns off
  prv_write_register(PmicRegisters_REG_STANDBY_MOD1, 0xa);

  // standby_mode_on (ReferenceControl bit 4)
  if (prv_set_register_bit(PmicRegisters_REFERENCE_CNTL, 4, true)) {
    while (1) {
    }
  }
  return false;
}

//! Cuts every rail, losing the backup domain. pmic_power_off() is usually
//! what you want.
bool pmic_full_power_off(void) {
  // ResetControl bit 1: power_off
  if (prv_set_register_bit(PmicRegisters_RESET_CNTL, 1, true)) {
    while (1) {
    }
  }
  return false;
}

//! Vsup cannot be read on silk; report the configured regulated value.
uint16_t pmic_get_vsys(void) {
  uint8_t cfg = 0;
  prv_read_register(PmicRegisters_CHARGE_CONFIG_1, &cfg);
  static const uint16_t vsup_mv[] = {4400, 4500, 4600, 4700, 4800, 4900, 5000, 5500};
  return vsup_mv[(cfg & 0x6) >> 1];
}

bool pmic_set_charger_state(bool enable) {
  // ChargerControl bit 5: enable charging from USB
  return prv_set_register_bit(PmicRegisters_CHARGE_CNTL, 5, enable);
}

bool pmic_is_charging(void) {
  uint8_t status;
  if (!prv_read_register(PmicRegisters_CHARGE_STATUS_1, &status)) {
    PBL_LOG_DBG("Failed to read charging status 1 register.");
    return false;
  }
  // ChargerStatus1 bits 0-3: CC, maintain/resume, trickle, CV
  return (status & 0x0f);
}

bool pmic_is_usb_connected(void) {
  uint8_t status;
  if (!prv_read_register(PmicRegisters_CHARGE_STATUS_2, &status)) {
    PBL_LOG_WRN("Failed to read charging status 2 register.");
    return false;
  }
  // ChargerStatus2 bit 2: charger detected
  return status & (1 << 2);
}

void pmic_read_chip_info(uint8_t *chip_id, uint8_t *chip_revision, uint8_t *buck1_vset) {
  prv_read_register(PmicRegisters_CHIP_ID, chip_id);
  prv_read_register(PmicRegisters_CHIP_REV, chip_revision);
  prv_read_register(PmicRegisters_SD1_VOLTAGE, buck1_vset);
}

bool pmic_enable_battery_measure(void) {
  // PMIC GPIO5 driven low enables the battery divider.
  return prv_set_pmic_gpio_outputs(PmicGpio5, 0);
}

bool pmic_disable_battery_measure(void) {
  return prv_set_pmic_gpio_outputs(0, PmicGpio5);
}

void set_ldo3_power_state(bool enabled) {
}

void set_4V5_power_state(bool enabled) {
}

void set_6V6_power_state(bool enabled) {
}

// Battery
///////////////////////////////////////////////////////////////////////////////

void battery_init(void) {
}

int battery_get_millivolts(void) {
  if (!s_present) {
    return BATTERY_NOMINAL_MV;
  }

  if (!pmic_enable_battery_measure()) {
    PBL_LOG_WRN("Failed to enable battery measure. Battery voltage reading will be bogus.");
  }

  uint32_t vref_total;
  uint32_t vmon_total;
  pbl_adc_stm32_read(AS3701B->vbat_adc_channel, &AS3701B->vbat_adc_pin, &vref_total, &vmon_total);

  pmic_disable_battery_measure();

  PBL_ASSERTN(AS3701B->vbat_scale_denominator);
  return pbl_adc_stm32_to_mv(vref_total, vmon_total, AS3701B->vbat_scale_numerator,
                             AS3701B->vbat_scale_denominator);
}

int battery_get_constants(BatteryConstants *constants) {
  // No current or temperature measurement on silk.
  constants->v_mv = battery_get_millivolts();
  constants->i_ua = 0;
  constants->t_mc = 25000;
  return 0;
}

int battery_charge_status_get(BatteryChargeStatus *status) {
  uint8_t chg_status;
  if (!prv_read_register(PmicRegisters_CHARGE_STATUS_1, &chg_status)) {
    return -EIO;
  }

  if (chg_status & (1 << 0)) {
    *status = BatteryChargeStatusCC;
  } else if (chg_status & (1 << 3)) {
    *status = BatteryChargeStatusCV;
  } else if (chg_status & (1 << 2)) {
    *status = BatteryChargeStatusTrickle;
  } else if (chg_status & (1 << 1)) {
    *status = BatteryChargeStatusComplete;
  } else {
    *status = BatteryChargeStatusUnknown;
  }

  return 0;
}

bool battery_charge_controller_thinks_we_are_charging_impl(void) {
  return pmic_is_charging();
}

bool battery_is_usb_connected_impl(void) {
  return pmic_is_usb_connected();
}

void battery_set_charge_enable(bool charging_enabled) {
  pmic_set_charger_state(charging_enabled);
}

//! The fast charge current is set by Rset; nothing to change at runtime.
void battery_set_fast_charge(bool fast_charge_enabled) {
}

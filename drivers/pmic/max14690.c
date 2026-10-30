/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Maxim MAX14690 PMIC on snowy. Register values are the stock firmware's.
//!
//! The battery and Vsys are read through the PMIC's MON output, which divides
//! the selected rail down into the ADC's range.

#include <errno.h>

#include <pbl/drivers/adc/stm32.h>
#include <pbl/drivers/battery.h>
#include <pbl/drivers/exti.h>
#include <pbl/drivers/pmic.h>
#include <pbl/drivers/pmic/max14690.h>
#include <pbl/kernel/mutex.h>
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/system_task.h>

#include <board/board.h>
#include <kernel/events.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

PBL_LOG_MODULE_DEFINE(driver_pmic, CONFIG_DRIVER_PMIC_LOG_LEVEL);

#define MAX14690_CHIP_ID 0x01

#define USB_CONN_DEBOUNCE_MS 1000

typedef enum {
  PmicRegisters_CHIP_ID = 0x00,
  PmicRegisters_CHIP_REV = 0x01,
  PmicRegisters_STATUSA = 0x02,
  PmicRegisters_STATUSB = 0x03,
  PmicRegisters_INTA = 0x05,
  PmicRegisters_INTB = 0x06,
  PmicRegisters_INT_MASK_A = 0x07,
  PmicRegisters_INT_MASK_B = 0x08,
  PmicRegisters_CHG_CNTL_A = 0x0A,
  PmicRegisters_CHG_CNTL_B = 0x0B,
  PmicRegisters_CH_TMR = 0x0C,
  PmicRegisters_BUCK1_CONFIG = 0x0D,
  PmicRegisters_BUCK1_VSET = 0x0E,
  PmicRegisters_BUCK2_CONFIG = 0x0F,
  PmicRegisters_LDO1_CONFIG = 0x12,
  PmicRegisters_LDO2_CONFIG = 0x14,
  PmicRegisters_LDO3_CONFIG = 0x16,
  PmicRegisters_MON_CFG = 0x19,
  PmicRegisters_HAND_SHK = 0x1D,
  PmicRegisters_PWR_CFG = 0x1F,
} PmicRegisters;

//! A rail routed to the MON output: ratio is the divider (1-4), source the
//! MON_CFG selection.
typedef struct {
  uint8_t ratio;
  uint8_t source;
} PmicMonConfig;

static const PmicMonConfig MON_VBAT = {3, 0b001};
static const PmicMonConfig MON_VSYS = {4, 0b010};

//! Held from selecting a MON source until it is deselected.
static PBL_MUTEX_DEFINE(s_mon_lock);

static TimerID s_debounce_usb_conn_timer = TIMER_INVALID_ID;

//! False if the PMIC did not answer at init (e.g. under emulation).
static bool s_present;

//! Reported when the battery cannot be measured. A made-up low value would
//! make the OS try (and fail) to enter standby through the absent PMIC.
#define BATTERY_NOMINAL_MV 3900

static bool prv_read_register(uint8_t reg, uint8_t *result) {
  return pbl_i2c_reg_read_byte(MAX14690->i2c, reg, result) == 0;
}

static bool prv_write_register(uint8_t reg, uint8_t value) {
  return pbl_i2c_reg_write_byte(MAX14690->i2c, reg, value) == 0;
}

//! Stock charger settings (PBL-15134, PBL-19186).
static bool prv_config_charger(void) {
  const uint8_t CHARGE_VOLTAGE_4300 = 0b101;

  // The charger's state machine can report "complete" when plugged in with
  // the battery between VBATREG - VBATRECHG and VBATREG, which is likely
  // given the battery's internal resistance. Raise VBATREG to 4.35 V, cycle
  // the charger, then set the real values so it recovers into charging.
  prv_write_register(PmicRegisters_CHG_CNTL_A, 0xCD);
  pmic_set_charger_state(false);
  pmic_set_charger_state(true);

  const uint8_t chg_ctrl_a = 1 << 7 |                   // Auto-stop
                             1 << 6 |                   // Auto-restart
                             0 << 4 |                   // 70 mV recharge threshold
                             CHARGE_VOLTAGE_4300 << 1 | // 4.30 V charge complete
                             1 << 0;                    // Charger enabled
  if (!prv_write_register(PmicRegisters_CHG_CNTL_A, chg_ctrl_a)) {
    return false;
  }

  const uint8_t chg_ctrl_b = 6 << 4 | // 3.00 V precharge threshold
                             1 << 2 | // 0.1C precharge current
                             1 << 0;  // 0.1C charge done current
  if (!prv_write_register(PmicRegisters_CHG_CNTL_B, chg_ctrl_b)) {
    return false;
  }

  const uint8_t ch_tmr = 1 << 4 | // 15 min maintain charge timeout
                         2 << 2 | // 300 min fast charge timeout
                         0 << 0;  // 30 min precharge timeout
  return prv_write_register(PmicRegisters_CH_TMR, ch_tmr);
}

//! Switch an LDO (XXXXX01X: enabled, XXXXX00X: disabled).
static bool prv_set_ldo(uint8_t config_reg, bool enable) {
  uint8_t val;
  if (!prv_read_register(config_reg, &val)) {
    return false;
  }
  val = (val & ~0x06) | (enable ? 0x02 : 0);
  if (!prv_write_register(config_reg, val)) {
    return false;
  }
  if (enable) {
    // The LDOs take 2.6 ms to ramp.
    pbl_sleep_ms(3);
  }
  return true;
}

static void prv_clear_pending_interrupts(void) {
  // Reading the interrupt registers clears them; an interrupt does not fire
  // while its bit is already set.
  uint8_t throwaway;
  prv_read_register(PmicRegisters_INTA, &throwaway);
  prv_read_register(PmicRegisters_INTB, &throwaway);
}

static void prv_debounce_callback(void *data) {
  const bool is_connected = pmic_is_usb_connected();
  if (is_connected) {
    // Configure the charger whenever the cable is connected.
    prv_config_charger();
  }

  uint8_t status_a = 0;
  uint8_t status_b = 0;
  prv_read_register(PmicRegisters_STATUSA, &status_a);
  prv_read_register(PmicRegisters_STATUSB, &status_b);
  PBL_LOG_INFO("PMIC plugged? %s StatusA 0x%x StatusB 0x%x", is_connected ? "YES" : "NO", status_a,
               status_b);

  PebbleEvent event = {
    .type = PEBBLE_BATTERY_CONNECTION_EVENT,
    .battery_connection = {
      .is_connected = is_connected,
    },
  };
  event_put(&event);
}

static void prv_handle_interrupt(void *data) {
  prv_clear_pending_interrupts();
  new_timer_start(s_debounce_usb_conn_timer, USB_CONN_DEBOUNCE_MS, prv_debounce_callback, nullptr,
                  0);
}

static void prv_interrupt_handler(void) {
  system_task_add_callback_from_isr(prv_handle_interrupt, nullptr);
}

static void prv_configure_interrupts(void) {
  const ExtiConfig exti = pbl_exti_stm32_config(&MAX14690->int_gpio);
  exti_configure_pin(exti, ExtiTrigger_Falling, prv_interrupt_handler);
  exti_enable(exti);

  // UsbOk only.
  prv_write_register(PmicRegisters_INT_MASK_A, 0x08);
  prv_clear_pending_interrupts();
}

static bool prv_is_alive(void) {
  uint8_t chip_id;
  if (!prv_read_register(PmicRegisters_CHIP_ID, &chip_id)) {
    return false;
  }
  if (chip_id != MAX14690_CHIP_ID) {
    PBL_LOG_ERR("MAX14690 whoami 0x%x, expecting 0x%x", chip_id, MAX14690_CHIP_ID);
    return false;
  }
  return true;
}

int pbl_max14690_init(const struct pbl_device *dev) {
  s_debounce_usb_conn_timer = new_timer_create();

  pbl_gpio_configure(&MAX14690->int_gpio, PBL_GPIO_INPUT | PBL_GPIO_PULL_UP);

  if (!prv_is_alive()) {
    PBL_LOG_ERR("MAX14690 not found; battery state unknown");
    return -ENODEV;
  }
  s_present = true;

  prv_config_charger();
  prv_configure_interrupts();
  // LDO2 (3.2 V) powers the display FPGA.
  if (!prv_set_ldo(PmicRegisters_LDO2_CONFIG, true)) {
    PBL_LOG_ERR("Could not enable LDO2");
  }

  return 0;
}

uint32_t pmic_get_last_reset_reason(void) {
  return 0;
}

int pmic_power_off(void) {
  if (prv_write_register(PmicRegisters_PWR_CFG, 0xB2)) {
    // The PMIC removes our power any time now.
    while (1) {
    }
  }
  return -EIO;
}

static void prv_set_mon_config_register(uint8_t value) {
  prv_write_register(PmicRegisters_MON_CFG, value);
}

//! Select a MON source. Takes the MON lock until prv_mon_release().
static void prv_mon_select(const PmicMonConfig *config) {
  pbl_mutex_lock(&s_mon_lock, PBL_FOREVER);
  // 4:1 is 0b00, 1:1 is 0b11.
  prv_set_mon_config_register(((4 - config->ratio) << 4) | config->source);
  // Let the reading settle: the line has some capacitance.
  pbl_busy_wait_us(200);
}

static void prv_mon_release(void) {
  prv_set_mon_config_register(0);
  pbl_mutex_unlock(&s_mon_lock);
}

static uint32_t prv_read_mon_mv(const PmicMonConfig *config) {
  uint32_t vref_total;
  uint32_t vmon_total;

  prv_mon_select(config);
  pbl_adc_stm32_read(MAX14690->mon_adc_channel, &MAX14690->mon_adc_pin, &vref_total, &vmon_total);
  prv_mon_release();

  return pbl_adc_stm32_to_mv(vref_total, vmon_total, config->ratio, 1);
}

bool pmic_enable_battery_measure(void) {
  return true;
}

bool pmic_disable_battery_measure(void) {
  return true;
}

uint16_t pmic_get_vsys(void) {
  if (!s_present) {
    return BATTERY_NOMINAL_MV;
  }
  return prv_read_mon_mv(&MON_VSYS);
}

bool pmic_set_charger_state(bool enable) {
  uint8_t val;
  if (!prv_read_register(PmicRegisters_CHG_CNTL_A, &val)) {
    return false;
  }
  if (enable) {
    val |= 0x01;
  } else {
    val &= ~0x01;
  }
  return prv_write_register(PmicRegisters_CHG_CNTL_A, val);
}

bool pmic_is_charging(void) {
  uint8_t val;
  if (!prv_read_register(PmicRegisters_STATUSA, &val)) {
    PBL_LOG_DBG("Failed to read charging status A register");
    return false;
  }

  const uint8_t chgstat = val & 0x07;
  return chgstat == 0x02 || // Pre-charge
         chgstat == 0x03 || // Fast charge, CC
         chgstat == 0x04 || // Fast charge, CV
         chgstat == 0x05;   // Maintain charge
}

bool pmic_is_usb_connected(void) {
  uint8_t val;
  if (!prv_read_register(PmicRegisters_STATUSB, &val)) {
    PBL_LOG_DBG("Failed to read charging status B register");
    return false;
  }
  return (val >> 3) & 1;
}

void pmic_read_chip_info(uint8_t *chip_id, uint8_t *chip_revision, uint8_t *buck1_vset) {
  prv_read_register(PmicRegisters_CHIP_ID, chip_id);
  prv_read_register(PmicRegisters_CHIP_REV, chip_revision);
  prv_read_register(PmicRegisters_BUCK1_VSET, buck1_vset);
}

//! LDO3 (1.8 V) powers the mic and the MFi chip; refcounted.
void set_ldo3_power_state(bool enabled) {
  static int s_ldo3_refs;

  pbl_mutex_lock(&s_mon_lock, PBL_FOREVER);
  if (enabled) {
    if (s_ldo3_refs++ == 0) {
      prv_set_ldo(PmicRegisters_LDO3_CONFIG, true);
    }
  } else if (s_ldo3_refs > 0) {
    if (--s_ldo3_refs == 0) {
      prv_set_ldo(PmicRegisters_LDO3_CONFIG, false);
    }
  }
  pbl_mutex_unlock(&s_mon_lock);
}

// The display driver switches the panel rails itself.
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
  return prv_read_mon_mv(&MON_VBAT);
}

int battery_get_constants(BatteryConstants *constants) {
  // No current or temperature measurement on snowy.
  constants->v_mv = battery_get_millivolts();
  constants->i_ua = 0;
  constants->t_mc = 25000;
  return 0;
}

int battery_charge_status_get(BatteryChargeStatus *status) {
  uint8_t val;
  if (!prv_read_register(PmicRegisters_STATUSA, &val)) {
    return -EIO;
  }

  switch (val & 0x07) {
    case 0x02:
      *status = BatteryChargeStatusTrickle;
      break;
    case 0x03:
      *status = BatteryChargeStatusCC;
      break;
    case 0x04:
      *status = BatteryChargeStatusCV;
      break;
    case 0x05:
    case 0x06:
      *status = BatteryChargeStatusComplete;
      break;
    default:
      *status = BatteryChargeStatusUnknown;
      break;
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

//! The fast charge current is set in hardware; nothing to change at runtime.
void battery_set_fast_charge(bool fast_charge_enabled) {
}

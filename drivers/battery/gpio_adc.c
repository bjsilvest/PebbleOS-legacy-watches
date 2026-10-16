/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Battery and charger without a PMIC (original Pebble): USB power and
//! charging status on GPIOs, optional charge enable and fast charge outputs,
//! and the battery voltage through a divider on the ADC. After the stock
//! battery_tintin driver.

#include <pbl/drivers/adc/stm32.h>
#include <pbl/drivers/battery.h>
#include <pbl/drivers/exti.h>
#include <pbl/logging/logging.h>
#include <pbl/services/new_timer/new_timer.h>
#include <pbl/services/system_task.h>

#include <board/board.h>
#include <kernel/events.h>
#include <system/passert.h>

#define USB_CONN_DEBOUNCE_MS 400

static TimerID s_debounce_timer = TIMER_INVALID_ID;
static bool s_debounced_usb_connected;

static bool prv_usb_connected_raw(void) {
  return pbl_gpio_get(&BOARD_CONFIG_POWER.vusb_stat) == 1;
}

static void prv_debounce_callback(void *data) {
  s_debounced_usb_connected = prv_usb_connected_raw();

  if (!s_debounced_usb_connected) {
    // Unplugged: leave the charger in a sane state for next time.
    battery_set_charge_enable(true);
    battery_set_fast_charge(false);
  }

  PebbleEvent event = {
    .type = PEBBLE_BATTERY_CONNECTION_EVENT,
    .battery_connection = {
      .is_connected = s_debounced_usb_connected,
    },
  };
  event_put(&event);
}

static void prv_start_debounce(void *data) {
  new_timer_start(s_debounce_timer, USB_CONN_DEBOUNCE_MS, prv_debounce_callback, nullptr, 0);
}

static void prv_vusb_interrupt_handler(void) {
  system_task_add_callback_from_isr(prv_start_debounce, nullptr);
}

void battery_init(void) {
  s_debounce_timer = new_timer_create();

  pbl_gpio_configure(&BOARD_CONFIG_POWER.vusb_stat, PBL_GPIO_INPUT);
  pbl_gpio_configure(&BOARD_CONFIG_POWER.chg_stat, PBL_GPIO_INPUT);
  if (pbl_gpio_is_connected(&BOARD_CONFIG_POWER.chg_fast)) {
    pbl_gpio_configure(&BOARD_CONFIG_POWER.chg_fast, PBL_GPIO_OUTPUT_INACTIVE);
  }
  if (pbl_gpio_is_connected(&BOARD_CONFIG_POWER.chg_en)) {
    pbl_gpio_configure(&BOARD_CONFIG_POWER.chg_en, PBL_GPIO_OUTPUT_ACTIVE);
  }

  const ExtiConfig exti = pbl_exti_stm32_config(&BOARD_CONFIG_POWER.vusb_stat);
  exti_configure_pin(exti, ExtiTrigger_RisingFalling, prv_vusb_interrupt_handler);
  exti_enable(exti);

  s_debounced_usb_connected = prv_usb_connected_raw();
}

int battery_get_millivolts(void) {
  uint32_t vref_total;
  uint32_t vmon_total;
  pbl_adc_stm32_read(BOARD_CONFIG_POWER.vbat_adc_channel, &BOARD_CONFIG_POWER.vbat_adc_pin,
                     &vref_total, &vmon_total);
  PBL_ASSERTN(BOARD_CONFIG_POWER.vbat_scale_denominator);
  return (int)pbl_adc_stm32_to_mv(vref_total, vmon_total, BOARD_CONFIG_POWER.vbat_scale_numerator,
                                  BOARD_CONFIG_POWER.vbat_scale_denominator);
}

int battery_get_constants(BatteryConstants *constants) {
  constants->v_mv = battery_get_millivolts();
  // No fuel gauge: current and temperature are not measured.
  constants->i_ua = 0;
  constants->t_mc = 25000;
  return 0;
}

int battery_charge_status_get(BatteryChargeStatus *status) {
  *status = BatteryChargeStatusUnknown;
  return 0;
}

bool battery_charge_controller_thinks_we_are_charging_impl(void) {
  return pbl_gpio_get(&BOARD_CONFIG_POWER.chg_stat) == 1;
}

bool battery_is_usb_connected_impl(void) {
  return s_debounced_usb_connected;
}

void battery_set_charge_enable(bool charging_enabled) {
  if (pbl_gpio_is_connected(&BOARD_CONFIG_POWER.chg_en)) {
    pbl_gpio_set(&BOARD_CONFIG_POWER.chg_en, charging_enabled);
  }
}

void battery_set_fast_charge(bool fast_charge_enabled) {
  if (pbl_gpio_is_connected(&BOARD_CONFIG_POWER.chg_fast)) {
    pbl_gpio_set(&BOARD_CONFIG_POWER.chg_fast, fast_charge_enabled);
  }
}

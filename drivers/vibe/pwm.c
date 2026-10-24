/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Single-direction vibe motor driven by PWM (silk: TI DRV2603), as the
//! stock firmware did it. The duty cycle is scaled by the supply voltage so
//! the strength does not change as the battery discharges.

#include <pbl/drivers/pmic.h>
#include <pbl/drivers/pwm.h>
#include <pbl/drivers/vibe.h>
#include <pbl/logging/logging.h>
#include <pbl/services/battery/battery_monitor.h>
#include <pbl/services/battery/battery_state.h>
#include <pbl/util/math.h>

#include <board/board.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>
#include <system/status_codes.h>

//! Duty cycles are integers in [0, 100].
#define PWM_TIMER_UPDATE_PERIOD (100)
// The DRV2603 operates in the [10, 250] kHz range.
#define PWM_OUTPUT_FREQUENCY_HZ (22 * 1000)
#define PWM_TIMER_FREQUENCY_HZ  (PWM_TIMER_UPDATE_PERIOD * PWM_OUTPUT_FREQUENCY_HZ)

#define PWM_DUTY_CYCLE_OFF  (0)
#define PWM_DUTY_CYCLE_FULL (PWM_TIMER_UPDATE_PERIOD)

static uint8_t s_vibe_duty_cycle = PWM_DUTY_CYCLE_FULL;
static bool s_initialized;

void vibe_init(void) {
  if (s_initialized) {
    return;
  }

  pwm_init(&BOARD_CONFIG_VIBE.pwm, PWM_TIMER_UPDATE_PERIOD, PWM_TIMER_FREQUENCY_HZ);
  s_initialized = true;
}

static uint16_t prv_get_vsys_mv(void) {
  if (battery_get_charge_state().is_plugged) {
    // Plugged in: the motor runs from Vsys rather than Vbat.
    return pmic_get_vsys();
  }
  return battery_state_get_voltage();
}

void vibe_set_strength(int8_t strength) {
  // Treat "reverse" strength as forward strength.
  uint32_t duty_cycle = ABS(strength);

  if (BOARD_CONFIG_VIBE.vsys_scale > 0) {
    const uint16_t vsys_mv = prv_get_vsys_mv();
    if (vsys_mv > 0) {
      duty_cycle = (BOARD_CONFIG_VIBE.vsys_scale * duty_cycle) / vsys_mv;
    }
  }

  s_vibe_duty_cycle = MIN(duty_cycle, PWM_DUTY_CYCLE_FULL);
}

static void prv_vibe_raw_ctl(bool on) {
  pwm_enable(&BOARD_CONFIG_VIBE.pwm, on);
  pwm_set_duty_cycle(&BOARD_CONFIG_VIBE.pwm, on ? s_vibe_duty_cycle : PWM_DUTY_CYCLE_OFF);
}

void vibe_ctl(bool on) {
  if (!s_initialized) {
    return;
  }

  if (on && battery_monitor_critical_lockout()) {
    on = false;
  }

  PBL_LOG_DBG("Vibe status <%s>", on ? "on" : "off");
  prv_vibe_raw_ctl(on);
}

void vibe_force_off(void) {
  if (!s_initialized) {
    return;
  }
  prv_vibe_raw_ctl(false);
}

int8_t vibe_get_braking_strength(void) {
  // Single direction: braking is just turning it off.
  return VIBE_STRENGTH_OFF;
}

status_t vibe_calibrate(void) {
  return E_INVALID_OPERATION;
}

uint8_t vibe_get_calibration(void) {
  return 0;
}

void vibe_apply_calibration(uint8_t cali) {
}

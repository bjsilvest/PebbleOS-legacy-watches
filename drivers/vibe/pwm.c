/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Single-direction vibe motor driven by PWM (silk: TI DRV2603), as the
//! stock firmware did it. The duty cycle is scaled by the supply voltage so
//! the strength does not change as the battery discharges.

#include <pbl/drivers/pmic.h>
#include <pbl/drivers/pwm.h>
#include <pbl/drivers/vibe.h>
#include <pbl/drivers/vibe/pwm.h>
#include <pbl/services/battery/battery_monitor.h>
#include <pbl/services/battery/battery_state.h>
#include <pbl/util/math.h>
#include <pbl/util/misc.h>

//! Duty cycles are integers in [0, 100].
#define PWM_TIMER_UPDATE_PERIOD (100)
// The DRV2603 operates in the [10, 250] kHz range.
#define PWM_OUTPUT_FREQUENCY_HZ (22 * 1000)
#define PWM_TIMER_FREQUENCY_HZ  (PWM_TIMER_UPDATE_PERIOD * PWM_OUTPUT_FREQUENCY_HZ)

#define PWM_DUTY_CYCLE_OFF  (0)
#define PWM_DUTY_CYCLE_FULL (PWM_TIMER_UPDATE_PERIOD)

static const struct pbl_vibe_pwm *prv_get(const struct pbl_vibe *vibe) {
  return container_of(vibe, const struct pbl_vibe_pwm, vibe);
}

static int prv_init(const struct pbl_vibe *vibe) {
  const struct pbl_vibe_pwm *dev = prv_get(vibe);

  pwm_init(&dev->pwm, PWM_TIMER_UPDATE_PERIOD, PWM_TIMER_FREQUENCY_HZ);

  return 0;
}

static uint16_t prv_get_vsys_mv(void) {
  if (battery_get_charge_state().is_plugged) {
    // Plugged in: the motor runs from Vsys rather than Vbat.
    return pmic_get_vsys();
  }
  return battery_state_get_voltage();
}

static int prv_start(const struct pbl_vibe *vibe, int8_t strength) {
  const struct pbl_vibe_pwm *dev = prv_get(vibe);
  // Treat "reverse" strength as forward strength.
  uint32_t duty_cycle = ABS(strength);

  if (battery_monitor_critical_lockout()) {
    duty_cycle = PWM_DUTY_CYCLE_OFF;
  } else if (dev->vsys_scale > 0U) {
    const uint16_t vsys_mv = prv_get_vsys_mv();
    if (vsys_mv > 0U) {
      duty_cycle = (dev->vsys_scale * duty_cycle) / vsys_mv;
    }
  }

  duty_cycle = MIN(duty_cycle, PWM_DUTY_CYCLE_FULL);
  pwm_enable(&dev->pwm, duty_cycle != PWM_DUTY_CYCLE_OFF);
  pwm_set_duty_cycle(&dev->pwm, duty_cycle);

  return 0;
}

static int prv_stop(const struct pbl_vibe *vibe) {
  const struct pbl_vibe_pwm *dev = prv_get(vibe);

  pwm_enable(&dev->pwm, false);
  pwm_set_duty_cycle(&dev->pwm, PWM_DUTY_CYCLE_OFF);

  return 0;
}

const struct pbl_vibe_ops pbl_vibe_pwm_ops = {
  .init = prv_init,
  .start = prv_start,
  .stop = prv_stop,
};

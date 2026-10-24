/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Vibe motor driven by PWM, as the stock firmware did it. Single direction
//! (silk: TI DRV2603): the duty cycle is scaled by the supply voltage so the
//! strength does not change as the battery discharges. H-bridge (snowy): 50%
//! is stopped and lower duty cycles run the motor in reverse, for braking.

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

#define PWM_DUTY_CYCLE_FULL (PWM_TIMER_UPDATE_PERIOD)

static const struct pbl_vibe_pwm *prv_get(const struct pbl_vibe *vibe) {
  return container_of(vibe, const struct pbl_vibe_pwm, vibe);
}

static uint32_t prv_duty_cycle_off(const struct pbl_vibe_pwm *dev) {
  return dev->hbridge ? PWM_TIMER_UPDATE_PERIOD / 2 : 0;
}

static int prv_init(const struct pbl_vibe *vibe) {
  const struct pbl_vibe_pwm *dev = prv_get(vibe);
  int ret;

  if (pbl_gpio_is_connected(&dev->ctl)) {
    ret = pbl_gpio_configure(&dev->ctl, PBL_GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
      return ret;
    }
  }

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

static uint32_t prv_duty_cycle(const struct pbl_vibe_pwm *dev, int8_t strength) {
  const uint32_t off = prv_duty_cycle_off(dev);

  if (dev->hbridge) {
    // Scale -100..100 to 0..100.
    return ((PWM_DUTY_CYCLE_FULL - off) * strength / VIBE_STRENGTH_MAX) + off;
  }

  // Treat "reverse" strength as forward strength.
  uint32_t duty_cycle = ABS(strength);

  if (dev->vsys_scale > 0U) {
    const uint16_t vsys_mv = prv_get_vsys_mv();
    if (vsys_mv > 0U) {
      duty_cycle = (dev->vsys_scale * duty_cycle) / vsys_mv;
    }
  }

  return MIN(duty_cycle, PWM_DUTY_CYCLE_FULL);
}

static void prv_raw_ctl(const struct pbl_vibe_pwm *dev, bool on, uint32_t duty_cycle) {
  pwm_enable(&dev->pwm, on);
  pwm_set_duty_cycle(&dev->pwm, on ? duty_cycle : prv_duty_cycle_off(dev));
  if (pbl_gpio_is_connected(&dev->ctl)) {
    (void)pbl_gpio_set(&dev->ctl, on);
  }
}

static int prv_start(const struct pbl_vibe *vibe, int8_t strength) {
  const struct pbl_vibe_pwm *dev = prv_get(vibe);

  if (battery_monitor_critical_lockout()) {
    prv_raw_ctl(dev, false, 0);
    return 0;
  }

  prv_raw_ctl(dev, true, prv_duty_cycle(dev, strength));

  return 0;
}

static int prv_stop(const struct pbl_vibe *vibe) {
  const struct pbl_vibe_pwm *dev = prv_get(vibe);

  prv_raw_ctl(dev, false, 0);

  return 0;
}

const struct pbl_vibe_ops pbl_vibe_pwm_ops = {
  .init = prv_init,
  .start = prv_start,
  .stop = prv_stop,
};

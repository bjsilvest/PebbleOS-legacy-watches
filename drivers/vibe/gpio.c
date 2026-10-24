/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Vibe motor switched by a GPIO (original Pebble): on or off, no strength.

#include <pbl/drivers/vibe.h>
#include <pbl/drivers/vibe/gpio.h>
#include <pbl/services/battery/battery_monitor.h>
#include <pbl/util/misc.h>

static const struct pbl_vibe_gpio *prv_get(const struct pbl_vibe *vibe) {
  return container_of(vibe, const struct pbl_vibe_gpio, vibe);
}

static int prv_init(const struct pbl_vibe *vibe) {
  return pbl_gpio_configure(&prv_get(vibe)->ctl, PBL_GPIO_OUTPUT_INACTIVE);
}

static int prv_start(const struct pbl_vibe *vibe, int8_t strength) {
  (void)strength;

  return pbl_gpio_set(&prv_get(vibe)->ctl, !battery_monitor_critical_lockout());
}

static int prv_stop(const struct pbl_vibe *vibe) {
  return pbl_gpio_set(&prv_get(vibe)->ctl, false);
}

const struct pbl_vibe_ops pbl_vibe_gpio_ops = {
  .init = prv_init,
  .start = prv_start,
  .stop = prv_stop,
};

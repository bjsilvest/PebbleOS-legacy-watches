/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Vibe motor switched by a GPIO (original Pebble): on or off, no strength.

#include <pbl/drivers/vibe.h>
#include <pbl/logging/logging.h>
#include <pbl/services/battery/battery_monitor.h>

#include <board/board.h>
#include <system/status_codes.h>

static bool s_initialized;

void vibe_init(void) {
  if (s_initialized) {
    return;
  }
  pbl_gpio_configure(&BOARD_CONFIG_VIBE.ctl, PBL_GPIO_OUTPUT_INACTIVE);
  s_initialized = true;
}

void vibe_set_strength(int8_t strength) {
}

void vibe_ctl(bool on) {
  if (!s_initialized) {
    return;
  }

  if (on && battery_monitor_critical_lockout()) {
    on = false;
  }

  PBL_LOG_DBG("Vibe status <%s>", on ? "on" : "off");
  pbl_gpio_set(&BOARD_CONFIG_VIBE.ctl, on);
}

void vibe_force_off(void) {
  if (!s_initialized) {
    return;
  }
  pbl_gpio_set(&BOARD_CONFIG_VIBE.ctl, false);
}

int8_t vibe_get_braking_strength(void) {
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

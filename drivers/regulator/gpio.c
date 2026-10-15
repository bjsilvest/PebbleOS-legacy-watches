/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/regulator/gpio.h>
#include <pbl/util/misc.h>

static const struct pbl_gpio *prv_enable_gpio(const struct pbl_regulator *reg) {
  return &container_of(reg, const struct pbl_gpio_regulator, reg)->enable;
}

static int prv_init(const struct pbl_regulator *reg) {
  return pbl_gpio_configure(prv_enable_gpio(reg), PBL_GPIO_OUTPUT_INACTIVE);
}

static int prv_enable(const struct pbl_regulator *reg) {
  return pbl_gpio_set(prv_enable_gpio(reg), true);
}

static int prv_disable(const struct pbl_regulator *reg) {
  return pbl_gpio_set(prv_enable_gpio(reg), false);
}

const struct pbl_regulator_ops pbl_gpio_regulator_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
};

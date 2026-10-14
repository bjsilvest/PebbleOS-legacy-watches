/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/init.h>

#include <stm32f4xx.h>

void pbl_soc_early_init(void) {
  SystemInit();
}

/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/irq.h>
#include <pbl/soc/stm32/sleep.h>

#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

static int s_block_count;

void soc_stm32_stop_block(void) {
  pbl_irq_lock();
  ++s_block_count;
  pbl_irq_unlock();
}

void soc_stm32_stop_release(void) {
  pbl_irq_lock();
  PBL_ASSERTN(s_block_count > 0);
  --s_block_count;
  pbl_irq_unlock();
}

bool soc_stm32_stop_is_allowed(void) {
  return s_block_count == 0;
}

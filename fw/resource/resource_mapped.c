/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include "resource_mapped.h"

#include <stdint.h>

#include <pbl/drivers/flash.h>
#include <pbl/kernel/irq.h>

#include <flash_region/flash_region.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

#if defined(CONFIG_MMAP_RESOURCES) && defined(FLASH_MEMORY_MAPPABLE_ADDRESS)
// Per task, so a process that exits holding mapped bytes releases them.
static uint16_t s_refs[NumPebbleTask];

void resource_mapped_use(PebbleTask task) {
  PBL_ASSERTN(task < NumPebbleTask);
  flash_use();
  pbl_irq_lock();
  s_refs[task]++;
  pbl_irq_unlock();
}

void resource_mapped_release(PebbleTask task) {
  PBL_ASSERTN(task < NumPebbleTask);
  pbl_irq_lock();
  PBL_ASSERTN(s_refs[task] > 0);
  s_refs[task]--;
  pbl_irq_unlock();
  flash_release_many(1);
}

void resource_mapped_release_all(PebbleTask task) {
  PBL_ASSERTN(task < NumPebbleTask);
  pbl_irq_lock();
  const uint32_t refs = s_refs[task];
  s_refs[task] = 0;
  pbl_irq_unlock();
  if (refs > 0) {
    flash_release_many(refs);
  }
}
#else
void resource_mapped_use(PebbleTask task) {
  (void)task;
}

void resource_mapped_release(PebbleTask task) {
  (void)task;
}

void resource_mapped_release_all(PebbleTask task) {
  (void)task;
}
#endif

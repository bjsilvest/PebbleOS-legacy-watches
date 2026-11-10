/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/irq.h>
#include <pbl/kernel/mutex.h>

#include <logging/logging_private.h>
#include <logging/pulse_logging.h>
#include <system/bootbits.h>
#include <system/reset.h>

void prepare_for_software_failure(void) {
#ifdef CONFIG_PULSE_EVERYWHERE
  pulse_logging_log_buffer_flush();
#endif

#ifndef CONFIG_MFG
  boot_bit_set(BOOT_BIT_SOFTWARE_FAILURE_OCCURRED);
#endif
}

[[noreturn]] void reset_due_to_software_failure(void) {
  prepare_for_software_failure();

#ifndef CONFIG_WATCHDOG
  // Don't reset right away, leave it in a state we can inspect

  pbl_irq_lock();
  while (1) {
    continue;
  }
#endif

  PBL_LOG_FROM_FAULT_HANDLER("Resetting!");

  // The orderly teardown in system_reset() can wait on a thread that needs a
  // lock held here (e.g. bt_lock); a locked-out scheduler skips to the dump.
  if (pbl_mutex_any_held()) {
    pbl_irq_lock();
  }
  system_reset();
}

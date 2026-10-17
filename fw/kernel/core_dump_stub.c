/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Without CONFIG_CORE_DUMP no memory image is saved: where one would be,
//! the watch resets. The reboot reason (with the faulting address) and the
//! software failure boot bit, which the stock bootloader counts towards
//! starting the recovery firmware, are set by the callers as before.

#include <kernel/core_dump.h>
#include <kernel/core_dump_private.h>
#include <system/reset.h>
#include <system/status_codes.h>

[[noreturn]] void core_dump_reset(bool is_forced) {
  system_hard_reset();
}

status_t core_dump_size(uint32_t flash_base, uint32_t *size) {
  return E_DOES_NOT_EXIST;
}

void core_dump_mark_read(uint32_t flash_base) {
}

bool core_dump_is_unread_available(uint32_t flash_base) {
  return false;
}

uint32_t core_dump_get_slot_address(unsigned int i) {
  return CORE_DUMP_FLASH_INVALID_ADDR;
}

bool core_dump_reserve_ble_slot(uint32_t *flash_base, uint32_t *max_size,
                                ElfExternalNote *build_id) {
  return false;
}

void core_dump_test_force_bus_fault(void) {
  system_hard_reset();
}

void core_dump_test_force_inf_loop(void) {
  system_hard_reset();
}

void core_dump_test_force_assert(void) {
  system_hard_reset();
}

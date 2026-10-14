/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_FLASH_GUARD
//! Returns true if [addr, addr + len) overlaps a region the firmware must
//! never erase or write (e.g. a stock PRF it does not own).
bool flash_guard_is_protected(uint32_t addr, uint32_t len);
#else
static inline bool flash_guard_is_protected(uint32_t addr, uint32_t len) {
  (void)addr;
  (void)len;
  return false;
}
#endif

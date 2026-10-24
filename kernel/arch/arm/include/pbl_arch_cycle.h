/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/kernel/compiler.h>

#include <pbl_arch_mmio.h>

#define ARCH_DWT_CTRL           0xE0001000UL
#define ARCH_DWT_CYCCNT         0xE0001004UL
#define ARCH_DWT_CTRL_CYCCNTENA (1UL << 0)

void arch_cycle_enable(void);

#ifdef CONFIG_ARCH_CYCLE_SOFT_FALLBACK
uint32_t arch_cycle_soft_get_32(void);
#endif

// Enabled on first use and re-enabled if a low power mode or a debugger turned it off.
static PBL_ALWAYS_INLINE uint32_t arch_cycle_get_32(void) {
  if ((arch_readl(ARCH_DWT_CTRL) & ARCH_DWT_CTRL_CYCCNTENA) == 0U) {
    arch_cycle_enable();
#ifdef CONFIG_ARCH_CYCLE_SOFT_FALLBACK
    // An emulator without the DWT: the enable does not stick
    if ((arch_readl(ARCH_DWT_CTRL) & ARCH_DWT_CTRL_CYCCNTENA) == 0U) {
      return arch_cycle_soft_get_32();
    }
#endif
  }
  return arch_readl(ARCH_DWT_CYCCNT);
}

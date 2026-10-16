/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/compiler.h>

#include <kernel.h>

// LTO does not see the branch below; keep kernel_prep_c linked in.
PBL_USED static void (*const s_kernel_prep_c_ref)(void) = kernel_prep_c;

[[noreturn]] PBL_NAKED void Reset_Handler(void) {
  __asm volatile(
#ifdef CONFIG_CPU_CORTEX_M_HAS_SPLIM
      "  ldr r0, =__isr_stack_start__ \n"
      "  msr msplim, r0 \n"
      "  mov r0, #0 \n"
      "  msr psplim, r0 \n"
#endif
      "  b kernel_prep_c \n"
      "  .ltorg \n");
}

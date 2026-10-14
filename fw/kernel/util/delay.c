/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "delay.h"

#include <pbl/kernel/compiler.h>

#ifdef CONFIG_SOC_NRF52
#include <drivers/nrfx_common.h>
#include <soc/nrfx_coredep.h>
#elifdef CONFIG_SOC_SF32LB52
#include <bf0_hal.h>
#elifdef CONFIG_SOC_POSIX
#include <unistd.h>
#elifdef CONFIG_SOC_STM32F4
#include <stm32f4xx.h>
#endif

#ifdef CONFIG_SOC_NRF52
void PBL_NOINLINE delay_us(uint32_t us) {
  nrfx_coredep_delay_us(us);
}

void delay_init(void) {
}

#elifdef CONFIG_SOC_SF32LB52
void PBL_NOINLINE delay_us(uint32_t us) {
  HAL_Delay_us(us);
}

void delay_init(void) {
}

#elifdef CONFIG_QEMU
#include <cmsis_core.h>

void PBL_NOINLINE delay_us(uint32_t us) {
  // Use DWT cycle counter for accurate delays
  uint32_t cycles = us * (SystemCoreClock / 1000000);
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < cycles) {
  }
}

void delay_init(void) {
  // Enable DWT cycle counter
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

#elifdef CONFIG_SOC_POSIX
void PBL_NOINLINE delay_us(uint32_t us) {
  usleep(us);
}

void delay_init(void) {
}

#elifdef CONFIG_SOC_STM32F4
// A calibrated spin loop: subs + taken bne is 3 cycles on the Cortex-M4.
#define CYCLES_PER_LOOP 3

static uint32_t s_loops_per_us;

void PBL_NOINLINE delay_us(uint32_t us) {
  uint32_t loops = us * s_loops_per_us;

  if (loops == 0) {
    return;
  }

  __asm volatile(
      "1:                \n"
      "  subs %[loops], #1 \n"
      "  bne 1b           \n"
      : [loops] "+r"(loops)
      :
      : "cc");
}

void delay_init(void) {
  // Round up so we never delay less than asked.
  s_loops_per_us = (SystemCoreClock / 1000000 + CYCLES_PER_LOOP - 1) / CYCLES_PER_LOOP;
}
#endif

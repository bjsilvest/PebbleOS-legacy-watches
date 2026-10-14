/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F4 independent watchdog (IWDG). The stock bootloader starts it
//! before jumping to the firmware, and it cannot be stopped once running.

#include <pbl/drivers/watchdog.h>

#include <stm32f4xx.h>

// LSI ~32 kHz / 64 = 500 Hz; 0xfff ticks is ~8 s, matching the bootloader.
#define IWDG_PRESCALER IWDG_Prescaler_64
#define IWDG_RELOAD    0xfff

void watchdog_init(void) {
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
  IWDG_SetPrescaler(IWDG_PRESCALER);
  IWDG_SetReload(IWDG_RELOAD);
  IWDG_WriteAccessCmd(IWDG_WriteAccess_Disable);

  // Freeze the watchdog while a debugger halts the core.
  DBGMCU_APB1PeriphConfig(DBGMCU_IWDG_STOP, ENABLE);
}

void watchdog_start(void) {
  IWDG_Enable();
  watchdog_feed();
}

void watchdog_stop(void) {
  // The IWDG cannot be stopped once started.
}

void watchdog_feed(void) {
  IWDG_ReloadCounter();
}

bool watchdog_check_reset_flag(void) {
  return RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET;
}

McuRebootReason watchdog_get_reset_flag(void) {
  return (McuRebootReason){
    .brown_out_reset = (RCC_GetFlagStatus(RCC_FLAG_BORRST) != RESET),
    .pin_reset = (RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET),
    .power_on_reset = (RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET),
    .software_reset = (RCC_GetFlagStatus(RCC_FLAG_SFTRST) != RESET),
    .independent_watchdog_reset = (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET),
    .window_watchdog_reset = (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET),
    .low_power_manager_reset = (RCC_GetFlagStatus(RCC_FLAG_LPWRRST) != RESET),
  };
}

McuRebootReason watchdog_clear_reset_flag(void) {
  const McuRebootReason reason = watchdog_get_reset_flag();
  RCC_ClearFlag();
  return reason;
}

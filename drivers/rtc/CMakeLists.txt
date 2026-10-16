# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources)
if(CONFIG_RTC_QEMU)
  list(APPEND sources qemu.c)
elseif(CONFIG_RTC_POSIX)
  list(APPEND sources posix.c)
elseif(CONFIG_RTC_NRF5)
  list(APPEND sources nrf5.c)
elseif(CONFIG_RTC_SF32LB)
  list(APPEND sources sf32lb.c)
elseif(CONFIG_RTC_STM32)
  list(APPEND sources stm32.c stm32_common.c)
elseif(CONFIG_RTC_STM32F2)
  list(APPEND sources stm32f2.c stm32_common.c)
endif()
pbl_library()
pbl_library_sources(${sources})

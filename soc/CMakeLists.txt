# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_QEMU)
  add_subdirectory(qemu)
elseif(CONFIG_SOC_NRF52)
  add_subdirectory(nrf)
elseif(CONFIG_SOC_SF32LB52)
  add_subdirectory(sf32lb)
elseif(CONFIG_SOC_STM32F4)
  add_subdirectory(stm32)
elseif(CONFIG_SOC_POSIX)
  add_subdirectory(posix)
endif()

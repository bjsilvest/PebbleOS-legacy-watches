# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

pbl_compile_definitions(USE_STDPERIPH_DRIVER=1)
if(CONFIG_HAL_STM32_STM32F205)
  # Same peripheral map as the F40x, which the F4 library knows
  pbl_compile_definitions(STM32F40_41xxx)
elseif(CONFIG_HAL_STM32_STM32F412)
  pbl_compile_definitions(STM32F412xG)
elseif(CONFIG_HAL_STM32_STM32F439)
  pbl_compile_definitions(STM32F429_439xx)
endif()
file(GLOB micro_sources CONFIGURE_DEPENDS
  stm32-sdk/STM32F4xx_StdPeriph_Driver/src/*.c
  stm32-sdk/OpenPDMFilter/*.c
)
if(CONFIG_HAL_STM32_STM32F412 OR CONFIG_HAL_STM32_STM32F205)
  # The F412 and the F2 have no FMC
  pbl_filter_sources(micro_sources EXCLUDE **/stm32f4xx_fmc.c)
else()
  # The F439 has the FMC instead of the FSMC
  pbl_filter_sources(micro_sources EXCLUDE **/stm32f4xx_fsmc.c)
endif()
set(micro_includes
  stm32-sdk/Device/ST/STM32F4xx/Include
  stm32-sdk/STM32F4xx_StdPeriph_Driver/inc
  stm32-sdk/OpenPDMFilter
)
pbl_include_directories(${micro_includes})
pbl_library_named(hal_stm32)
pbl_library_kind(stlib)
pbl_library_sources(${micro_sources})
pbl_library_compile_options(-Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare)

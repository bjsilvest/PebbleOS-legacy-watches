# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(sources)
if(CONFIG_DISPLAY_QEMU)
  list(APPEND sources qemu.c)
elseif(CONFIG_DISPLAY_SDL)
  list(APPEND sources sdl/display_sdl.c)
  pbl_host_sources(sdl/display_sdl_bottom.c)
elseif(CONFIG_DISPLAY_JDI_SF32LB)
  list(APPEND sources sf32lb/display_jdi.c)
  if(CONFIG_SHELL)
    list(APPEND sources sf32lb/display_jdi_shell.c)
  endif()
elseif(CONFIG_DISPLAY_SHARP_LS013B7DH01_NRF5)
  list(APPEND sources sharp_ls013b7dh01/sharp_ls013b7dh01_nrf5.c)
elseif(CONFIG_DISPLAY_SHARP_LS013B7DH01_STM32)
  list(APPEND sources sharp_ls013b7dh01/sharp_ls013b7dh01_stm32.c)
elseif(CONFIG_DISPLAY_ICE40LP_STM32)
  if(CONFIG_BOARD_SPALDING)
    # iCE40UL1K, 180x180 round
    set(bitstream ${CMAKE_CURRENT_SOURCE_DIR}/ice40lp/spalding_framebuffer.fpga)
  else()
    set(bitstream ${CMAKE_CURRENT_SOURCE_DIR}/ice40lp/snowy_framebuffer.fpga)
  endif()
  list(APPEND sources ice40lp/ice40lp_stm32.c ice40lp/ice40lp_bitstream.c)
  set_source_files_properties(ice40lp/ice40lp_bitstream.c PROPERTIES
    OBJECT_DEPENDS ${bitstream}
    COMPILE_DEFINITIONS "ICE40LP_BITSTREAM_PATH=\"${bitstream}\"")
endif()
pbl_library()
pbl_library_sources(${sources})

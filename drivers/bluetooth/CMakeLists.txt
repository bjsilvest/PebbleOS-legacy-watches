# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BT_CONTROLLER_DA1468X)
  set(images)
  foreach(image STAGE2 CONTROLLER)
    set(path "${CONFIG_BT_DA1468X_${image}_IMAGE}")
    if(NOT path OR NOT EXISTS "${path}")
      message(FATAL_ERROR
        "CONFIG_BT_DA1468X_${image}_IMAGE must name an existing file, see docs/boards/silk")
    endif()
    string(APPEND paths_h "#define DA1468X_${image}_PATH \"${path}\"\n")
    list(APPEND images "${path}")
  endforeach()

  set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/da1468x_gen)
  file(CONFIGURE OUTPUT ${gen_dir}/da1468x_image_paths.h CONTENT "${paths_h}")
  set_source_files_properties(da1468x_images.c PROPERTIES
    OBJECT_DEPENDS "${images}"
    INCLUDE_DIRECTORIES ${gen_dir})

  pbl_library()
  pbl_library_sources(da1468x.c da1468x_images.c)
endif()

if(CONFIG_BT_CONTROLLER_CC2564)
  pbl_library()
  pbl_library_sources(cc2564.c)
  if(NOT CONFIG_BT_CC2564_BTS_RESOURCE)
    set(bts ${PBL_BASE}/third_party/ti_bt_sp/TIInit_6.7.16.bts)
    set_source_files_properties(cc2564_bts.c PROPERTIES
      OBJECT_DEPENDS ${bts}
      COMPILE_DEFINITIONS "CC2564_BTS_PATH=\"${bts}\"")
    pbl_library_sources(cc2564_bts.c)
  endif()
endif()

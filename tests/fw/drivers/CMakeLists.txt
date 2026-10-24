# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_clar_test(test_flash_api
  SOURCES
    drivers/flash/flash_api.c
  OVERRIDES dummy_board
)

pbl_clar_test(test_flash_erase
  SOURCES
    drivers/flash/flash_erase.c
  PLATFORMS emery
  OVERRIDES dummy_board
)

pbl_clar_test(test_accel_qemu
  SOURCES
    drivers/base/device.c
    drivers/imu/accel.c
    drivers/imu/qemu.c
    tests/fakes/fake_rtc.c
  DEFINES
    CONFIG_DEVICE_LOG_LEVEL=0
    PBL_NO_LINKER_SCRIPT
  OVERRIDES dummy_board
)

pbl_clar_test(test_flash_guard
  SOURCES
    drivers/flash/flash_guard.c
  DEFINES CONFIG_FLASH_LAYOUT_SILK=1 CONFIG_FLASH_GUARD=1
  OVERRIDES dummy_board
)

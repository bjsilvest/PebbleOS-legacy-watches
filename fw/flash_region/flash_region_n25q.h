/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

// Original Pebble (tintin) 4 MiB Micron N25Q032 SPI NOR, in the stock layout.
// The stock bootloader installs updates from FIRMWARE_SLOT_1 (its firmware
// scratch) and boots SAFE_FIRMWARE (PRF); PRF also reads
// STOCK_SHARED_PRF_STORAGE and the factory registry (MFG_INFO). Those keep
// their stock addresses and are never written (see flash_guard). The OS keeps
// its own SHARED_PRF_STORAGE in the stock layout's spare space.

#define SECTOR_SIZE_BYTES    0x10000
#define SECTOR_ADDR_MASK     (~(SECTOR_SIZE_BYTES - 1))
#define SUBSECTOR_SIZE_BYTES 0x1000
#define SUBSECTOR_ADDR_MASK  (~(SUBSECTOR_SIZE_BYTES - 1))

#define FLASH_FILESYSTEM_BLOCK_SIZE SUBSECTOR_SIZE_BYTES

#define BOARD_NOR_FLASH_SIZE 0x400000

// Firmware scratch space the stock bootloader installs updates from
#define FLASH_REGION_FIRMWARE_SLOT_1_BEGIN 0x0
#define FLASH_REGION_FIRMWARE_SLOT_1_END   0x80000 // 512k

#define FLASH_REGION_FILESYSTEM_3_BEGIN 0x80000
#define FLASH_REGION_FILESYSTEM_3_END   0x100000 // 512k

// The stock PRF's shared storage (formerly the registry): read only
#define FLASH_REGION_STOCK_SHARED_PRF_STORAGE_BEGIN 0x100000
#define FLASH_REGION_STOCK_SHARED_PRF_STORAGE_END   0x110000 // 64k

// Factory settings registry (watch color, RTC calibration): read only
#define FLASH_REGION_MFG_INFO_BEGIN 0x110000
#define FLASH_REGION_MFG_INFO_END   0x120000 // 64k

#define FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN 0x120000
#define FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END   0x160000 // 256k

// The stock layout's spare sectors (the stock PRF erases them on a factory
// reset): the OS's own shared PRF storage, then one unused sector
#define FLASH_REGION_SHARED_PRF_STORAGE_BEGIN 0x160000
#define FLASH_REGION_SHARED_PRF_STORAGE_END   0x170000 // 64k

#define FLASH_REGION_FILESYSTEM_5_BEGIN 0x180000
#define FLASH_REGION_FILESYSTEM_5_END   0x200000 // 512k

#define FLASH_REGION_SAFE_FIRMWARE_BEGIN 0x200000
#define FLASH_REGION_SAFE_FIRMWARE_END   0x280000 // 512k

#define FLASH_REGION_SYSTEM_RESOURCES_BANK_1_BEGIN 0x280000
#define FLASH_REGION_SYSTEM_RESOURCES_BANK_1_END   0x2c0000 // 256k

#define FLASH_REGION_FILESYSTEM_2_BEGIN 0x2c0000
#define FLASH_REGION_FILESYSTEM_2_END   0x2d0000 // 64k

#define FLASH_REGION_FILESYSTEM_BEGIN 0x2d0000
#define FLASH_REGION_FILESYSTEM_END   0x320000 // 320k

#define FLASH_REGION_FILESYSTEM_4_BEGIN 0x320000
#define FLASH_REGION_FILESYSTEM_4_END   0x3e0000 // 768k

#define FLASH_REGION_DEBUG_DB_BEGIN 0x3e0000
#define FLASH_REGION_DEBUG_DB_END   0x400000 // 128k
#define FLASH_DEBUG_DB_BLOCK_SIZE   SUBSECTOR_SIZE_BYTES

static_assert(FLASH_REGION_DEBUG_DB_END == BOARD_NOR_FLASH_SIZE, "tintin flash is 4 MiB");

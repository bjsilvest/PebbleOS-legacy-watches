/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

// Pebble Time (snowy) Spansion S29VS parallel NOR, memory mapped through the
// FMC. The layout is fixed by the stock bootloader and PRF: SAFE_FIRMWARE,
// STOCK_SHARED_PRF_STORAGE and MFG_INFO (which also holds the bootloader's
// FPGA bitstream) must stay where they are and are never written (see
// flash_guard).
//
// The first 128K is "bottom boot": 32K erase sectors. Everything else uses
// 128K sectors.

#define SECTOR_SIZE_BYTES (0x20000)
#define SECTOR_ADDR_MASK  (~(SECTOR_SIZE_BYTES - 1))

#define SUBSECTOR_SIZE_BYTES (0x20000)
#define SUBSECTOR_ADDR_MASK  (~(SUBSECTOR_SIZE_BYTES - 1))

#define BOTTOM_BOOT_REGION_END  0x20000
#define BOTTOM_BOOT_SECTOR_SIZE 0x8000

// The 640K the stock layout leaves free holds the OS's own SHARED_PRF_STORAGE;
// the stock PRF's copy (STOCK_SHARED_PRF_STORAGE) is never written.
#define FLASH_REGION_DEF(MACRO, arg)                                                   \
  MACRO(DEBUG_DB, 0x020000 /*   128K */, arg)                /* 0x000000 - 0x020000 */ \
  MACRO(SHARED_PRF_STORAGE, 0x020000 /*   128K */, arg)      /* 0x020000 - 0x040000 */ \
  MACRO(RSVD_OS, 0x080000 /*   512K */, arg)                 /* 0x040000 - 0x0C0000 */ \
  MACRO(STOCK_SHARED_PRF_STORAGE, 0x020000 /* 128K */, arg)  /* 0x0C0000 - 0x0E0000 */ \
  MACRO(MFG_INFO, 0x020000 /*   128K */, arg)                /* 0x0E0000 - 0x100000 */ \
  MACRO(FIRMWARE_SLOT_1, 0x100000 /*  1024K */, arg)         /* 0x100000 - 0x200000 */ \
  MACRO(SAFE_FIRMWARE, 0x100000 /*  1024K */, arg)           /* 0x200000 - 0x300000 */ \
  MACRO(SYSTEM_RESOURCES_BANK_0, 0x080000 /*   512K */, arg) /* 0x300000 - 0x380000 */ \
  MACRO(SYSTEM_RESOURCES_BANK_1, 0x080000 /*   512K */, arg) /* 0x380000 - 0x400000 */ \
  MACRO(FILESYSTEM, 0xC00000 /* 12288K */, arg)              /* 0x400000 - 0x1000000 */

#include "flash_region_def_helper.h"

//! The NOR is memory mapped through the FMC (bank 1) at this address. Reading
//! it needs the flash in use (flash_use()): idle, its clock and pins are off.
#define FLASH_MEMORY_MAPPABLE_ADDRESS 0x60000000U

// Flash region _BEGIN and _END addresses
//////////////////////////////////////////////////////////////////////////////

#define FLASH_REGION_DEBUG_DB_BEGIN FLASH_REGION_START_ADDR(DEBUG_DB)
#define FLASH_REGION_DEBUG_DB_END   FLASH_REGION_END_ADDR(DEBUG_DB)
#define FLASH_DEBUG_DB_BLOCK_SIZE   BOTTOM_BOOT_SECTOR_SIZE

#define FLASH_REGION_SHARED_PRF_STORAGE_BEGIN FLASH_REGION_START_ADDR(SHARED_PRF_STORAGE)
#define FLASH_REGION_SHARED_PRF_STORAGE_END   FLASH_REGION_END_ADDR(SHARED_PRF_STORAGE)

#define FLASH_REGION_STOCK_SHARED_PRF_STORAGE_BEGIN \
  FLASH_REGION_START_ADDR(STOCK_SHARED_PRF_STORAGE)
#define FLASH_REGION_STOCK_SHARED_PRF_STORAGE_END FLASH_REGION_END_ADDR(STOCK_SHARED_PRF_STORAGE)

#define FLASH_REGION_MFG_INFO_BEGIN FLASH_REGION_START_ADDR(MFG_INFO)
#define FLASH_REGION_MFG_INFO_END   FLASH_REGION_END_ADDR(MFG_INFO)

// The stock bootloader installs updates from FIRMWARE_SCRATCH, which is
// FIRMWARE_SLOT_1 here.
#define FLASH_REGION_FIRMWARE_SLOT_1_BEGIN FLASH_REGION_START_ADDR(FIRMWARE_SLOT_1)
#define FLASH_REGION_FIRMWARE_SLOT_1_END   FLASH_REGION_END_ADDR(FIRMWARE_SLOT_1)

#define FLASH_REGION_SAFE_FIRMWARE_BEGIN FLASH_REGION_START_ADDR(SAFE_FIRMWARE)
#define FLASH_REGION_SAFE_FIRMWARE_END   FLASH_REGION_END_ADDR(SAFE_FIRMWARE)

#define FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN FLASH_REGION_START_ADDR(SYSTEM_RESOURCES_BANK_0)
#define FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END   FLASH_REGION_END_ADDR(SYSTEM_RESOURCES_BANK_0)

#define FLASH_REGION_SYSTEM_RESOURCES_BANK_1_BEGIN FLASH_REGION_START_ADDR(SYSTEM_RESOURCES_BANK_1)
#define FLASH_REGION_SYSTEM_RESOURCES_BANK_1_END   FLASH_REGION_END_ADDR(SYSTEM_RESOURCES_BANK_1)

#define FLASH_REGION_FILESYSTEM_BEGIN FLASH_REGION_START_ADDR(FILESYSTEM)
#define FLASH_REGION_FILESYSTEM_END   FLASH_REGION_END_ADDR(FILESYSTEM)
#define FLASH_FILESYSTEM_BLOCK_SIZE   0x2000

#define BOARD_NOR_FLASH_SIZE (FLASH_REGION_START_ADDR(_COUNT) - FLASH_REGION_BASE_ADDRESS)

// Static asserts to make sure everything worked out
//////////////////////////////////////////////////////////////////////////////

FLASH_REGION_SIZE_CHECK(BOTTOM_BOOT_SECTOR_SIZE)

static_assert(BOARD_NOR_FLASH_SIZE == 0x1000000, "snowy flash must be 16 MiB");

// Protected regions must stay at their stock addresses.
static_assert(FLASH_REGION_SAFE_FIRMWARE_BEGIN - FLASH_REGION_BASE_ADDRESS == 0x200000,
              "PRF moved");
static_assert(FLASH_REGION_SAFE_FIRMWARE_END - FLASH_REGION_BASE_ADDRESS == 0x300000,
              "PRF resized");
static_assert(FLASH_REGION_STOCK_SHARED_PRF_STORAGE_BEGIN - FLASH_REGION_BASE_ADDRESS == 0x0C0000,
              "stock shared PRF storage moved");
static_assert(FLASH_REGION_MFG_INFO_BEGIN - FLASH_REGION_BASE_ADDRESS == 0x0E0000,
              "MFG info moved");
static_assert(FLASH_REGION_MFG_INFO_END - FLASH_REGION_BASE_ADDRESS == 0x100000,
              "MFG info resized");
static_assert(FLASH_REGION_FIRMWARE_SLOT_1_BEGIN - FLASH_REGION_BASE_ADDRESS == 0x100000,
              "firmware scratch moved");
static_assert(FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN - FLASH_REGION_BASE_ADDRESS == 0x300000,
              "resources moved");
static_assert(FLASH_REGION_FILESYSTEM_BEGIN - FLASH_REGION_BASE_ADDRESS == 0x400000,
              "filesystem moved");

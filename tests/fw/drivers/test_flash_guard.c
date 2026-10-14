/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/flash/flash_guard.h>

#include <clar.h>
#include <flash_region/flash_region.h>

static void prv_check_region(uint32_t begin, uint32_t end) {
  cl_assert(!flash_guard_is_protected(begin - 1, 1));
  cl_assert(!flash_guard_is_protected(begin - SUBSECTOR_SIZE_BYTES, SUBSECTOR_SIZE_BYTES));
  cl_assert(flash_guard_is_protected(begin, 1));
  cl_assert(flash_guard_is_protected(end - 1, 1));
  cl_assert(flash_guard_is_protected(begin - 1, 2));
  cl_assert(flash_guard_is_protected(end - 1, 2));
  cl_assert(flash_guard_is_protected(begin, end - begin));
  cl_assert(!flash_guard_is_protected(end, 1));
  cl_assert(!flash_guard_is_protected(begin, 0));
}

void test_flash_guard__prf(void) {
  prv_check_region(FLASH_REGION_SAFE_FIRMWARE_BEGIN, FLASH_REGION_SAFE_FIRMWARE_END);
}

void test_flash_guard__stock_shared_prf_storage(void) {
  // MFG info directly follows, so only check the start edge here.
  const uint32_t begin = FLASH_REGION_STOCK_SHARED_PRF_STORAGE_BEGIN;
  cl_assert(!flash_guard_is_protected(begin - 1, 1));
  cl_assert(flash_guard_is_protected(begin, 1));
  cl_assert(flash_guard_is_protected(begin - 1, 2));
}

void test_flash_guard__mfg_info(void) {
  const uint32_t begin = FLASH_REGION_MFG_INFO_BEGIN;
  const uint32_t end = FLASH_REGION_MFG_INFO_END;
  cl_assert(flash_guard_is_protected(begin, 1));
  cl_assert(flash_guard_is_protected(end - 1, 1));
  cl_assert(flash_guard_is_protected(begin, end - begin));
}

void test_flash_guard__os_regions_writable(void) {
  cl_assert(!flash_guard_is_protected(
      FLASH_REGION_FIRMWARE_SLOT_1_BEGIN,
      FLASH_REGION_FIRMWARE_SLOT_1_END - FLASH_REGION_FIRMWARE_SLOT_1_BEGIN));
  cl_assert(!flash_guard_is_protected(
      FLASH_REGION_SYSTEM_RESOURCES_BANK_1_BEGIN,
      FLASH_REGION_SYSTEM_RESOURCES_BANK_1_END - FLASH_REGION_SYSTEM_RESOURCES_BANK_1_BEGIN));
  cl_assert(!flash_guard_is_protected(FLASH_REGION_FILESYSTEM_BEGIN,
                                      FLASH_REGION_FILESYSTEM_END - FLASH_REGION_FILESYSTEM_BEGIN));
  cl_assert(!flash_guard_is_protected(
      FLASH_REGION_SHARED_PRF_STORAGE_BEGIN,
      FLASH_REGION_SHARED_PRF_STORAGE_END - FLASH_REGION_SHARED_PRF_STORAGE_BEGIN));
  cl_assert(!flash_guard_is_protected(FLASH_REGION_TZINFO_BEGIN, SUBSECTOR_SIZE_BYTES));
}

void test_flash_guard__oversized_length(void) {
  // A length that would wrap 32 bits must still be seen as covering PRF.
  cl_assert(flash_guard_is_protected(FLASH_REGION_FIRMWARE_SLOT_1_BEGIN, UINT32_MAX));
  // Resources bank 1 runs right up to PRF; overflowing it must be caught.
  cl_assert(flash_guard_is_protected(
      FLASH_REGION_SYSTEM_RESOURCES_BANK_1_BEGIN,
      FLASH_REGION_SYSTEM_RESOURCES_BANK_1_END - FLASH_REGION_SYSTEM_RESOURCES_BANK_1_BEGIN + 1));
}

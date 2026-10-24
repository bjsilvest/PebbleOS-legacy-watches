/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! Macronix MX25U64 on silk.
//!
//! Stock firmware may have switched this part to individual block protection
//! (WPSEL, a one-time programmable bit). In that mode every block powers up
//! locked, so we unlock everything and lock again the regions this firmware
//! must never write (PRF, stock shared PRF storage, MFG info): a hardware
//! backstop for the flash guard. WPSEL itself is never set from here.

#include <pbl/drivers/flash/flash_impl.h>
#include <pbl/drivers/flash/qspi_flash.h>
#include <pbl/drivers/flash/qspi_flash_part_definitions.h>
#include <pbl/logging/logging.h>
#include <pbl/util/math.h>

#include <board/board.h>
#include <flash_region/flash_region.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>
#include <system/status_codes.h>

static QSPIFlashPart QSPI_FLASH_PART = {
  .instructions =
      {
        .fast_read = 0x0B,
        .pp = 0x02,
        .erase_sector_4k = 0x20,
        .erase_block_64k = 0xD8,
        .write_enable = 0x06,
        .write_disable = 0x04,
        .rdsr1 = 0x05,
        .rdsr2 = 0x2B,
        .erase_suspend = 0xB0,
        .erase_resume = 0x30,
        .enter_low_power = 0xB9,
        .exit_low_power = 0xAB,
        .enter_quad_mode = 0x35,
        .reset_enable = 0x66,
        .reset = 0x99,
        .qspi_id = 0xAF,

        .block_lock = 0x36,
        .block_lock_status = 0x3C,
        .block_unlock_all = 0x98,

        // WPSEL (0x68) is deliberately absent: it cannot be undone.
        .read_protection_status = 0x2B,
      },
  .status_bit_masks =
      {
        .busy = 1 << 0,
        .write_enable = 1 << 1,
      },
  .flag_status_bit_masks =
      {
        .erase_suspend = 1 << 3,
      },
  .dummy_cycles =
      {
        .fast_read = 4,
      },
  .block_lock =
      {
        .has_lock_data = false,
        .locked_check = 0xff,
        .protection_enabled_mask = (1 << 7), // WPSEL in the security register
      },
  .reset_latency_ms = 13,
  .suspend_to_read_latency_us = 20,
  .standby_to_low_power_latency_us = 10,
  .low_power_to_standby_latency_us = 30,
  .supports_fast_read_ddr = false,
  .supports_block_lock = true,
  .qspi_id_value = 0x3725c2,
  .size = 0x800000,
  .name = "MX25U64",
};

//! Block locks are only available once WPSEL is set.
static bool s_block_lock_mode;

bool flash_check_whoami(void) {
  return qspi_flash_check_whoami(QSPI_FLASH);
}

FlashAddress flash_impl_get_sector_base_address(FlashAddress addr) {
  return (addr & SECTOR_ADDR_MASK);
}

FlashAddress flash_impl_get_subsector_base_address(FlashAddress addr) {
  return (addr & SUBSECTOR_ADDR_MASK);
}

//! Lock [start, end) with the part's lock granularity: 4K in the first and
//! last 64K blocks, 64K elsewhere.
static status_t prv_lock_range(FlashAddress start, FlashAddress end) {
  FlashAddress block_addr = start;
  while (block_addr < end) {
    uint32_t block_size;
    if (WITHIN(block_addr, SECTOR_SIZE_BYTES, BOARD_NOR_FLASH_SIZE - SECTOR_SIZE_BYTES - 1)) {
      block_addr = flash_impl_get_sector_base_address(block_addr);
      block_size = SECTOR_SIZE_BYTES;
    } else {
      block_addr = flash_impl_get_subsector_base_address(block_addr);
      block_size = SUBSECTOR_SIZE_BYTES;
    }
    const status_t sc = qspi_flash_lock_sector(QSPI_FLASH, block_addr);
    if (FAILED(sc)) {
      return sc;
    }
    block_addr += block_size;
  }

  return S_SUCCESS;
}

static void prv_lock_stock_regions(void) {
  if (!s_block_lock_mode) {
    return;
  }

  status_t sc = prv_lock_range(FLASH_REGION_SAFE_FIRMWARE_BEGIN, FLASH_REGION_SAFE_FIRMWARE_END);
  sc |= prv_lock_range(FLASH_REGION_STOCK_SHARED_PRF_STORAGE_BEGIN, FLASH_REGION_MFG_INFO_END);
  if (FAILED(sc)) {
    PBL_LOG_ERR("Failed to lock PRF/MFG flash blocks");
  }
}

void flash_impl_enable_write_protection(void) {
}

status_t flash_impl_write_protect(FlashAddress start_sector, FlashAddress end_sector) {
  if (!s_block_lock_mode) {
    // The flash guard still covers it.
    return S_SUCCESS;
  }

  return prv_lock_range(start_sector, end_sector + 1);
}

status_t flash_impl_unprotect(void) {
  if (!s_block_lock_mode) {
    return S_SUCCESS;
  }

  // Never leave the stock regions unlocked.
  qspi_flash_unlock_all(QSPI_FLASH);
  prv_lock_stock_regions();
  return S_SUCCESS;
}

status_t flash_impl_init(bool coredump_mode) {
  qspi_flash_init(QSPI_FLASH, &QSPI_FLASH_PART, coredump_mode);

  s_block_lock_mode = qspi_flash_protection_is_enabled(QSPI_FLASH);
  if (s_block_lock_mode) {
    // Every block powers up locked in this mode.
    qspi_flash_unlock_all(QSPI_FLASH);
    prv_lock_stock_regions();
  }

  return S_SUCCESS;
}

status_t flash_impl_get_erase_status(void) {
  return qspi_flash_is_erase_complete(QSPI_FLASH);
}

status_t flash_impl_erase_subsector_begin(FlashAddress subsector_addr) {
  return qspi_flash_erase_begin(QSPI_FLASH, subsector_addr, true /* is_subsector */);
}

status_t flash_impl_erase_sector_begin(FlashAddress sector_addr) {
  return qspi_flash_erase_begin(QSPI_FLASH, sector_addr, false /* !is_subsector */);
}

status_t flash_impl_erase_bulk_begin(void) {
  // A chip erase would wipe the PRF and MFG info.
  return E_INVALID_OPERATION;
}

status_t flash_impl_erase_suspend(FlashAddress sector_addr) {
  return qspi_flash_erase_suspend(QSPI_FLASH, sector_addr);
}

status_t flash_impl_erase_resume(FlashAddress sector_addr) {
  qspi_flash_erase_resume(QSPI_FLASH, sector_addr);
  return S_SUCCESS;
}

status_t flash_impl_read_sync(void *buffer_ptr, FlashAddress start_addr, size_t buffer_size) {
  PBL_ASSERT(buffer_size > 0, "flash_impl_read_sync() called with 0 bytes to read");
  qspi_flash_read_blocking(QSPI_FLASH, start_addr, buffer_ptr, buffer_size);
  return S_SUCCESS;
}

int flash_impl_write_page_begin(const void *buffer, const FlashAddress start_addr, size_t len) {
  return qspi_flash_write_page_begin(QSPI_FLASH, buffer, start_addr, len);
}

status_t flash_impl_get_write_status(void) {
  return qspi_flash_get_write_status(QSPI_FLASH);
}

status_t flash_impl_enter_low_power_mode(void) {
  qspi_flash_set_lower_power_mode(QSPI_FLASH, true);
  return S_SUCCESS;
}

status_t flash_impl_exit_low_power_mode(void) {
  qspi_flash_set_lower_power_mode(QSPI_FLASH, false);
  return S_SUCCESS;
}

status_t flash_impl_set_burst_mode(bool burst_mode) {
  return S_SUCCESS;
}

status_t flash_impl_blank_check_sector(FlashAddress addr) {
  return qspi_flash_blank_check(QSPI_FLASH, addr, false /* !is_subsector */);
}

status_t flash_impl_blank_check_subsector(FlashAddress addr) {
  return qspi_flash_blank_check(QSPI_FLASH, addr, true /* is_subsector */);
}

uint32_t flash_impl_get_typical_sector_erase_duration_ms(void) {
  return 400;
}

uint32_t flash_impl_get_typical_subsector_erase_duration_ms(void) {
  return 40;
}

size_t flash_impl_get_capacity(void) {
  return BOARD_NOR_FLASH_SIZE;
}

status_t flash_impl_read_security_register(uint32_t addr, uint8_t *val) {
  return qspi_flash_read_security_register(QSPI_FLASH, addr, val);
}

status_t flash_impl_security_register_is_locked(uint32_t address, bool *locked) {
  return qspi_flash_security_register_is_locked(QSPI_FLASH, address, locked);
}

status_t flash_impl_erase_security_register(uint32_t addr) {
  return qspi_flash_erase_security_register(QSPI_FLASH, addr);
}

status_t flash_impl_write_security_register(uint32_t addr, uint8_t val) {
  return qspi_flash_write_security_register(QSPI_FLASH, addr, val);
}

const FlashSecurityRegisters *flash_impl_security_registers_info(void) {
  return qspi_flash_security_registers_info(QSPI_FLASH);
}

#ifdef CONFIG_RECOVERY_FW
status_t flash_impl_lock_security_register(uint32_t address) {
  return qspi_flash_lock_security_register(QSPI_FLASH, address);
}
#endif

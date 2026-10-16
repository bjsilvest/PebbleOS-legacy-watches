/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! Micron N25Q032 SPI NOR (original Pebble), on a plain SPI bus with a
//! software chip select. Transfers are polled. Commands from the stock
//! micron_n25q driver; the safe firmware (PRF) is held locked with the
//! volatile sector lock register, as the stock firmware did.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/flash/flash_impl.h>
#include <pbl/logging/logging.h>

#include <board/board.h>
#include <flash_region/flash_region.h>
#include <kernel/util/delay.h>
#include <stm32f4xx.h>
#include <system/passert.h>
#include <system/status_codes.h>

PBL_LOG_MODULE_DECLARE(driver_flash, CONFIG_DRIVER_FLASH_LOG_LEVEL);

#define CMD_WRITE_ENABLE          0x06
#define CMD_READ_STATUS_REG       0x05
#define CMD_READ_FLAG_STATUS_REG  0x70
#define CMD_CLEAR_FLAG_STATUS_REG 0x50
#define CMD_READ                  0x03
#define CMD_READ_ID               0x9F
#define CMD_PAGE_PROGRAM          0x02
#define CMD_ERASE_SUBSECTOR       0x20
#define CMD_ERASE_SECTOR          0xD8
#define CMD_DEEP_SLEEP            0xB9
#define CMD_WAKE                  0xAB
#define CMD_SUSPEND               0x75
#define CMD_RESUME                0x7A
#define CMD_WRITE_LOCK_REGISTER   0xE5

#define STATUS_WRITE_IN_PROGRESS (1 << 0)

#define FLAG_SECTOR_LOCKED     (1 << 1)
#define FLAG_PROGRAM_SUSPENDED (1 << 2)
#define FLAG_PROGRAM_ERROR     (1 << 4)
#define FLAG_ERASE_ERROR       (1 << 5)
#define FLAG_ERASE_SUSPENDED   (1 << 6)

#define LOCK_SECTOR_WRITE (1 << 0)

#define PAGE_SIZE_BYTES 256

// Micron N25Q032A (1.8 V and 3 V parts), and what QEMU's bb2 machine emulates.
static const uint32_t s_known_ids[] = {0x20bb16, 0x20ba16, 0x1c3016};

// tRES1: deep power-down release time.
#define WAKE_DELAY_US 30

static uint32_t s_num_flash_uses;
static bool s_deep_sleep;

///////////////////////////////////////////////////////////////////////////////
// SPI

static void prv_select(void) {
  pbl_gpio_set(&BOARD_CONFIG_NOR_SPI.scs, true);
}

static void prv_deselect(void) {
  SPI_TypeDef *const spi = BOARD_CONFIG_NOR_SPI.spi;
  while (spi->SR & SPI_SR_BSY) {
  }
  pbl_gpio_set(&BOARD_CONFIG_NOR_SPI.scs, false);
  // tSHSL: 50 ns between commands
  delay_us(1);
}

static uint8_t prv_xfer(uint8_t byte) {
  SPI_TypeDef *const spi = BOARD_CONFIG_NOR_SPI.spi;
  while (!(spi->SR & SPI_SR_TXE)) {
  }
  spi->DR = byte;
  while (!(spi->SR & SPI_SR_RXNE)) {
  }
  return (uint8_t)spi->DR;
}

static void prv_send_addr(FlashAddress addr) {
  PBL_ASSERTN((addr & 0xff000000) == 0);
  prv_xfer((uint8_t)(addr >> 16));
  prv_xfer((uint8_t)(addr >> 8));
  prv_xfer((uint8_t)addr);
}

static void prv_cmd(uint8_t cmd) {
  prv_select();
  prv_xfer(cmd);
  prv_deselect();
}

static uint8_t prv_read_reg(uint8_t cmd) {
  prv_select();
  prv_xfer(cmd);
  const uint8_t val = prv_xfer(0);
  prv_deselect();
  return val;
}

static void prv_wait_ready(void) {
  while (prv_read_reg(CMD_READ_STATUS_REG) & STATUS_WRITE_IN_PROGRESS) {
  }
}

static void prv_hw_init(void) {
  RCC_APB2PeriphClockCmd(BOARD_CONFIG_NOR_SPI.spi_rcc, ENABLE);

  pbl_gpio_stm32_af(&BOARD_CONFIG_NOR_SPI.sclk, BOARD_CONFIG_NOR_SPI.spi_af, PBL_GPIO_PULL_DOWN,
                    GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&BOARD_CONFIG_NOR_SPI.miso, BOARD_CONFIG_NOR_SPI.spi_af, 0, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&BOARD_CONFIG_NOR_SPI.mosi, BOARD_CONFIG_NOR_SPI.spi_af, 0, GPIO_Speed_50MHz);
  pbl_gpio_configure(&BOARD_CONFIG_NOR_SPI.scs, PBL_GPIO_OUTPUT_INACTIVE);

  // SPI1 is on APB2 (48 MHz): /2 gives 24 MHz, well under the 54 MHz read limit.
  SPI_InitTypeDef spi_cfg;
  SPI_I2S_DeInit(BOARD_CONFIG_NOR_SPI.spi);
  SPI_StructInit(&spi_cfg);
  spi_cfg.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  spi_cfg.SPI_Mode = SPI_Mode_Master;
  spi_cfg.SPI_DataSize = SPI_DataSize_8b;
  spi_cfg.SPI_CPOL = SPI_CPOL_Low;
  spi_cfg.SPI_CPHA = SPI_CPHA_1Edge;
  spi_cfg.SPI_NSS = SPI_NSS_Soft;
  spi_cfg.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
  spi_cfg.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_Init(BOARD_CONFIG_NOR_SPI.spi, &spi_cfg);
  SPI_Cmd(BOARD_CONFIG_NOR_SPI.spi, ENABLE);
}

///////////////////////////////////////////////////////////////////////////////
// Use counting: the SPI clock runs only while the flash is in use

void flash_impl_use(void) {
  if (s_num_flash_uses == 0) {
    RCC_APB2PeriphClockCmd(BOARD_CONFIG_NOR_SPI.spi_rcc, ENABLE);
  }
  s_num_flash_uses++;
}

void flash_impl_release_many(uint32_t num_locks) {
  PBL_ASSERTN(s_num_flash_uses >= num_locks);
  s_num_flash_uses -= num_locks;
  if (s_num_flash_uses == 0) {
    RCC_APB2PeriphClockCmd(BOARD_CONFIG_NOR_SPI.spi_rcc, DISABLE);
  }
}

void flash_impl_release(void) {
  flash_impl_release_many(1);
}

///////////////////////////////////////////////////////////////////////////////
// Protection

static void prv_set_prf_lock(bool lock) {
  for (FlashAddress addr = FLASH_REGION_SAFE_FIRMWARE_BEGIN; addr < FLASH_REGION_SAFE_FIRMWARE_END;
       addr += SECTOR_SIZE_BYTES) {
    prv_cmd(CMD_WRITE_ENABLE);
    prv_select();
    prv_xfer(CMD_WRITE_LOCK_REGISTER);
    prv_send_addr(addr);
    prv_xfer(lock ? LOCK_SECTOR_WRITE : 0);
    prv_deselect();
  }
}

void flash_impl_enable_write_protection(void) {
}

status_t flash_impl_write_protect(FlashAddress start_sector, FlashAddress end_sector) {
  // The only range ever locked is PRF.
  if (start_sector != FLASH_REGION_SAFE_FIRMWARE_BEGIN ||
      end_sector != FLASH_REGION_SAFE_FIRMWARE_END - SECTOR_SIZE_BYTES) {
    PBL_LOG_WRN("Not locking 0x%" PRIx32 "-0x%" PRIx32 ", only PRF", start_sector, end_sector);
  }
  flash_impl_use();
  prv_set_prf_lock(true);
  flash_impl_release();
  return S_SUCCESS;
}

status_t flash_impl_unprotect(void) {
  // Never leave PRF unlocked.
  return S_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// Init and identification

status_t flash_impl_init(bool coredump_mode) {
  prv_hw_init();
  flash_impl_use();
  // The flash may still be in deep power-down from before a reset
  prv_cmd(CMD_WAKE);
  delay_us(WAKE_DELAY_US);
  s_deep_sleep = false;
  prv_wait_ready();
  prv_cmd(CMD_CLEAR_FLAG_STATUS_REG);
  prv_set_prf_lock(true);
  flash_impl_release();
  return S_SUCCESS;
}

static uint32_t prv_read_id(void) {
  flash_impl_use();
  prv_select();
  prv_xfer(CMD_READ_ID);
  uint32_t id = (uint32_t)prv_xfer(0) << 16;
  id |= (uint32_t)prv_xfer(0) << 8;
  id |= prv_xfer(0);
  prv_deselect();
  flash_impl_release();
  return id;
}

bool flash_check_whoami(void) {
  const uint32_t id = prv_read_id();
  PBL_LOG_DBG("Flash ID: 0x%06" PRIx32, id);
  for (size_t i = 0; i < sizeof(s_known_ids) / sizeof(s_known_ids[0]); i++) {
    if (id == s_known_ids[i]) {
      return true;
    }
  }
  return false;
}

size_t flash_impl_get_capacity(void) {
  return BOARD_NOR_FLASH_SIZE;
}

FlashAddress flash_impl_get_sector_base_address(FlashAddress addr) {
  return addr & SECTOR_ADDR_MASK;
}

FlashAddress flash_impl_get_subsector_base_address(FlashAddress addr) {
  return addr & SUBSECTOR_ADDR_MASK;
}

status_t flash_impl_set_burst_mode(bool enable) {
  return S_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// Low power: deep power-down while the MCU stops

status_t flash_impl_enter_low_power_mode(void) {
  // Called with interrupts off on the way into STOP; only sleep when idle.
  if (s_num_flash_uses != 0 || s_deep_sleep) {
    return S_SUCCESS;
  }
  flash_impl_use();
  if (!(prv_read_reg(CMD_READ_STATUS_REG) & STATUS_WRITE_IN_PROGRESS)) {
    prv_cmd(CMD_DEEP_SLEEP);
    s_deep_sleep = true;
  }
  flash_impl_release();
  return S_SUCCESS;
}

status_t flash_impl_exit_low_power_mode(void) {
  if (!s_deep_sleep) {
    return S_SUCCESS;
  }
  flash_impl_use();
  prv_cmd(CMD_WAKE);
  delay_us(WAKE_DELAY_US);
  s_deep_sleep = false;
  flash_impl_release();
  return S_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// Read

status_t flash_impl_read_sync(void *buffer, FlashAddress addr, size_t len) {
  uint8_t *out = buffer;
  flash_impl_use();
  prv_select();
  // Plain read, as stock: no dummy cycles, fine at our SPI clock.
  prv_xfer(CMD_READ);
  prv_send_addr(addr);
  for (size_t i = 0; i < len; i++) {
    out[i] = prv_xfer(0);
  }
  prv_deselect();
  flash_impl_release();
  return S_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// Program

int flash_impl_write_page_begin(const void *buffer, FlashAddress addr, size_t len) {
  if (len == 0) {
    return E_INVALID_ARGUMENT;
  }
  // Stay within the page: past its end the write would wrap to its start.
  const size_t room = PAGE_SIZE_BYTES - (addr % PAGE_SIZE_BYTES);
  const size_t count = (len < room) ? len : room;
  const uint8_t *in = buffer;

  flash_impl_use();
  if (prv_read_reg(CMD_READ_STATUS_REG) & STATUS_WRITE_IN_PROGRESS) {
    flash_impl_release();
    return E_BUSY;
  }
  prv_cmd(CMD_CLEAR_FLAG_STATUS_REG);
  prv_cmd(CMD_WRITE_ENABLE);
  prv_select();
  prv_xfer(CMD_PAGE_PROGRAM);
  prv_send_addr(addr);
  for (size_t i = 0; i < count; i++) {
    prv_xfer(in[i]);
  }
  prv_deselect();
  flash_impl_release();
  return (int)count;
}

static status_t prv_op_status(uint8_t suspended_flag, uint8_t error_flag) {
  flash_impl_use();
  const uint8_t status = prv_read_reg(CMD_READ_STATUS_REG);
  const uint8_t flags = prv_read_reg(CMD_READ_FLAG_STATUS_REG);
  flash_impl_release();

  if (status & STATUS_WRITE_IN_PROGRESS) {
    return E_BUSY;
  }
  if (flags & suspended_flag) {
    return E_AGAIN;
  }
  if (flags & (error_flag | FLAG_SECTOR_LOCKED)) {
    return E_ERROR;
  }
  return S_SUCCESS;
}

status_t flash_impl_get_write_status(void) {
  return prv_op_status(FLAG_PROGRAM_SUSPENDED, FLAG_PROGRAM_ERROR);
}

static status_t prv_suspend(uint8_t suspended_flag) {
  flash_impl_use();
  status_t status;
  if (!(prv_read_reg(CMD_READ_STATUS_REG) & STATUS_WRITE_IN_PROGRESS)) {
    // Nothing running: it finished before we got here.
    status = S_NO_ACTION_REQUIRED;
  } else {
    prv_cmd(CMD_SUSPEND);
    prv_wait_ready();
    status = (prv_read_reg(CMD_READ_FLAG_STATUS_REG) & suspended_flag) ? S_SUCCESS
                                                                       : S_NO_ACTION_REQUIRED;
  }
  flash_impl_release();
  return status;
}

static status_t prv_resume(uint8_t suspended_flag) {
  flash_impl_use();
  status_t status = E_INVALID_OPERATION;
  if (prv_read_reg(CMD_READ_FLAG_STATUS_REG) & suspended_flag) {
    prv_cmd(CMD_RESUME);
    status = S_SUCCESS;
  }
  flash_impl_release();
  return status;
}

status_t flash_impl_write_suspend(FlashAddress addr) {
  return prv_suspend(FLAG_PROGRAM_SUSPENDED);
}

status_t flash_impl_write_resume(FlashAddress addr) {
  return prv_resume(FLAG_PROGRAM_SUSPENDED);
}

///////////////////////////////////////////////////////////////////////////////
// Erase

static status_t prv_erase_begin(uint8_t cmd, FlashAddress addr) {
  flash_impl_use();
  status_t result = S_SUCCESS;
  const uint8_t flags = prv_read_reg(CMD_READ_FLAG_STATUS_REG);
  if (prv_read_reg(CMD_READ_STATUS_REG) & STATUS_WRITE_IN_PROGRESS) {
    result = E_BUSY;
  } else if (flags & (FLAG_ERASE_SUSPENDED | FLAG_PROGRAM_SUSPENDED)) {
    // Cannot start an erase while another operation is suspended.
    result = E_INVALID_OPERATION;
  } else {
    prv_cmd(CMD_CLEAR_FLAG_STATUS_REG);
    prv_cmd(CMD_WRITE_ENABLE);
    prv_select();
    prv_xfer(cmd);
    prv_send_addr(addr);
    prv_deselect();
  }
  flash_impl_release();
  return result;
}

status_t flash_impl_erase_subsector_begin(FlashAddress subsector_addr) {
  return prv_erase_begin(CMD_ERASE_SUBSECTOR,
                         flash_impl_get_subsector_base_address(subsector_addr));
}

status_t flash_impl_erase_sector_begin(FlashAddress sector_addr) {
  return prv_erase_begin(CMD_ERASE_SECTOR, flash_impl_get_sector_base_address(sector_addr));
}

// A bulk erase would take PRF and the factory registry with it.
status_t flash_impl_erase_bulk_begin(void) {
  return E_INVALID_OPERATION;
}

status_t flash_impl_get_erase_status(void) {
  return prv_op_status(FLAG_ERASE_SUSPENDED, FLAG_ERASE_ERROR);
}

status_t flash_impl_erase_suspend(FlashAddress addr) {
  return prv_suspend(FLAG_ERASE_SUSPENDED);
}

status_t flash_impl_erase_resume(FlashAddress addr) {
  return prv_resume(FLAG_ERASE_SUSPENDED);
}

// Datasheet typicals
uint32_t flash_impl_get_typical_sector_erase_duration_ms(void) {
  return 700;
}

uint32_t flash_impl_get_typical_subsector_erase_duration_ms(void) {
  return 250;
}

///////////////////////////////////////////////////////////////////////////////
// Blank check, in software

static status_t prv_blank_check(FlashAddress base, uint32_t size) {
  flash_impl_use();
  prv_select();
  prv_xfer(CMD_READ);
  prv_send_addr(base);
  status_t ret = S_TRUE;
  for (uint32_t i = 0; i < size; i++) {
    if (prv_xfer(0) != 0xff) {
      ret = S_FALSE;
      break;
    }
  }
  prv_deselect();
  flash_impl_release();
  return ret;
}

status_t flash_impl_blank_check_sector(FlashAddress addr) {
  return prv_blank_check(flash_impl_get_sector_base_address(addr), SECTOR_SIZE_BYTES);
}

status_t flash_impl_blank_check_subsector(FlashAddress addr) {
  return prv_blank_check(flash_impl_get_subsector_base_address(addr), SUBSECTOR_SIZE_BYTES);
}

///////////////////////////////////////////////////////////////////////////////
// Security registers: not used

static const FlashSecurityRegisters s_security_regs = {};

const FlashSecurityRegisters *flash_impl_security_registers_info(void) {
  return &s_security_regs;
}

status_t flash_impl_read_security_register(uint32_t addr, uint8_t *val) {
  return E_INVALID_ARGUMENT;
}

status_t flash_impl_security_register_is_locked(uint32_t address, bool *locked) {
  return E_INVALID_ARGUMENT;
}

status_t flash_impl_erase_security_register(uint32_t addr) {
  return E_INVALID_ARGUMENT;
}

status_t flash_impl_write_security_register(uint32_t addr, uint8_t val) {
  return E_INVALID_ARGUMENT;
}

#ifdef CONFIG_RECOVERY_FW
status_t flash_impl_lock_security_register(uint32_t address) {
  return E_INVALID_ARGUMENT;
}
#endif

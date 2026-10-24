/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F412 QUADSPI and the QSPI flash protocol on top of it.
//!
//! The flash runs in QPI mode: instruction, address and data all on 4 lines.
//! Reads are polled through the FIFO for now (no DMA or memory mapping).
//! Callers (flash_api) serialize access.

#include <string.h>

#include <pbl/drivers/flash/flash_impl.h>
#include <pbl/drivers/flash/qspi_flash.h>
#include <pbl/drivers/flash/qspi_flash_definitions.h>
#include <pbl/drivers/gpio/stm32.h>
#include <pbl/logging/logging.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

#include <board/board.h>
#include <flash_region/flash_region.h>
#include <pbl/kernel/delay.h>
#include <kernel/util/sleep.h>
#include <stm32f4xx.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>
#include <system/status_codes.h>

//! Address value which signifies no address being sent
#define QSPI_ADDR_NO_ADDR (UINT32_MAX)

//! qspi_poll_bit() without a timeout
#define QSPI_NO_TIMEOUT (UINT32_MAX)

#define FLASH_RESET_WORD_VALUE (0xffffffff)

// QUADSPI port
///////////////////////////////////////////////////////////////////////////////

static void prv_port_init(QSPIPort *dev) {
  if (dev->state->initialized) {
    return;
  }

  pbl_gpio_stm32_af(&dev->cs_gpio, dev->cs_af, 0, GPIO_Speed_100MHz);
  pbl_gpio_stm32_af(&dev->clk_gpio, dev->clk_af, 0, GPIO_Speed_100MHz);
  for (int i = 0; i < QSPI_NUM_DATA_PINS; i++) {
    pbl_gpio_stm32_af(&dev->data_gpio[i], dev->data_af, 0, GPIO_Speed_100MHz);
  }

  // Never clock the flash faster than asked for.
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);
  uint32_t prescaler = clocks.HCLK_Frequency / dev->clock_speed_hz;
  if ((clocks.HCLK_Frequency / prescaler) > dev->clock_speed_hz) {
    prescaler++;
  }

  RCC->AHB3ENR |= RCC_AHB3ENR_QSPIEN;
  (void)RCC->AHB3ENR;

  // FSIZE: the flash has 2^(FSIZE + 1) bytes.
  PBL_ASSERTN(dev->flash_size == (uint32_t)1 << ceil_log_two(dev->flash_size));

  QSPI_InitTypeDef qspi_config;
  QSPI_StructInit(&qspi_config);
  qspi_config.QSPI_SShift = QSPI_SShift_HalfCycleShift;
  // QSPI clock = AHB / (1 + QSPI_Prescaler)
  qspi_config.QSPI_Prescaler = prescaler - 1;
  qspi_config.QSPI_CKMode = QSPI_CKMode_Mode0;
  qspi_config.QSPI_CSHTime = QSPI_CSHTime_1Cycle;
  qspi_config.QSPI_FSize = ceil_log_two(dev->flash_size) - 1;
  qspi_config.QSPI_FSelect = QSPI_FSelect_1;
  qspi_config.QSPI_DFlash = QSPI_DFlash_Disable;
  QSPI_Init(&qspi_config);
  QSPI_Cmd(ENABLE);

  dev->state->initialized = true;
}

static void prv_set_num_data_bytes(uint32_t length) {
  // DLR holds the number of bytes minus one.
  PBL_ASSERTN(length > 0);
  QSPI_SetDataLength(length - 1);
}

static void prv_wait_for_transfer_complete(void) {
  while (QSPI_GetFlagStatus(QSPI_FLAG_TC) == RESET) {
  }
}

static void prv_wait_for_not_busy(void) {
  while (QSPI_GetFlagStatus(QSPI_FLAG_BUSY) != RESET) {
  }
}

static void prv_read_bytes(uint8_t *buffer, size_t buffer_size) {
  for (size_t i = 0; i < buffer_size; ++i) {
    buffer[i] = QSPI_ReceiveData8();
  }
}

static void prv_set_sdr(void) {
  PBL_ASSERTN(!QSPI_GetFlagStatus(QSPI_FLAG_BUSY));
  QUADSPI->CR |= QUADSPI_CR_SSHIFT;
}

//! CCR fields we set; reserved bits are left alone.
#define QSPI_CCR_CLEAR_MASK                                                                  \
  (~(QUADSPI_CCR_INSTRUCTION | QUADSPI_CCR_IMODE | QUADSPI_CCR_ADMODE | QUADSPI_CCR_ADSIZE | \
     QUADSPI_CCR_ABMODE | QUADSPI_CCR_ABSIZE | QUADSPI_CCR_DCYC | QUADSPI_CCR_DMODE |        \
     QUADSPI_CCR_FMODE | QUADSPI_CCR_SIOO | QUADSPI_CCR_DHHC | QUADSPI_CCR_DDRM))

static void prv_set_comm_config(uint32_t modes_bitset, uint32_t dummy_cycles) {
  uint32_t ccr = QUADSPI->CCR;
  ccr &= QSPI_CCR_CLEAR_MASK;
  ccr |= modes_bitset;
  ccr |= (dummy_cycles << 18);
  QUADSPI->CCR = ccr;
}

static void prv_indirect_read(uint8_t instruction, uint32_t addr, uint8_t dummy_cycles,
                              void *buffer, uint32_t length) {
  prv_set_num_data_bytes(length);
  prv_set_sdr();

  uint32_t modes_bitset = QSPI_ComConfig_DDRMode_Disable | QSPI_ComConfig_DHHC_Disable |
                          QSPI_ComConfig_FMode_Indirect_Read | QSPI_ComConfig_DMode_4Line |
                          QSPI_ComConfig_IMode_4Line | instruction;
  if (addr != QSPI_ADDR_NO_ADDR) {
    modes_bitset |= QSPI_ComConfig_ADMode_4Line | QSPI_ComConfig_ADSize_24bit;
  }
  prv_set_comm_config(modes_bitset, dummy_cycles);

  if (addr != QSPI_ADDR_NO_ADDR) {
    QSPI_SetAddress(addr);
  }

  prv_read_bytes(buffer, length);

  QSPI_ClearFlag(QSPI_FLAG_TC);
  prv_wait_for_not_busy();
}

static void prv_indirect_write(uint8_t instruction, uint32_t addr, const void *buffer,
                               uint32_t length) {
  if (length) {
    PBL_ASSERTN(buffer);
    prv_set_num_data_bytes(length);
  } else {
    PBL_ASSERTN(!buffer);
  }

  prv_set_sdr();

  uint32_t modes_bitset =
      QSPI_ComConfig_FMode_Indirect_Write | QSPI_ComConfig_IMode_4Line | instruction;
  if (addr != QSPI_ADDR_NO_ADDR) {
    modes_bitset |= QSPI_ComConfig_ADMode_4Line | QSPI_ComConfig_ADSize_24bit;
  }
  if (length) {
    modes_bitset |= QSPI_ComConfig_DMode_4Line;
  }
  prv_set_comm_config(modes_bitset, 0);

  if (addr != QSPI_ADDR_NO_ADDR) {
    QSPI_SetAddress(addr);
  }

  const uint8_t *read_ptr = buffer;
  for (uint32_t i = 0; i < length; ++i) {
    // Stalls the CPU while the FIFO is full.
    QSPI_SendData8(read_ptr[i]);
  }

  prv_wait_for_transfer_complete();
  QSPI_ClearFlag(QSPI_FLAG_TC);
  prv_wait_for_not_busy();
}

//! A 1-line instruction, used to switch the flash into QPI mode.
static void prv_indirect_write_no_addr_1line(uint8_t instruction) {
  prv_set_sdr();
  prv_set_comm_config(
      QSPI_ComConfig_FMode_Indirect_Write | QSPI_ComConfig_IMode_1Line | instruction, 0);

  prv_wait_for_transfer_complete();
  QSPI_ClearFlag(QSPI_FLAG_TC);
  prv_wait_for_not_busy();
}

static bool prv_poll_bit(QSPIPort *dev, uint8_t instruction, uint8_t bit_mask, bool should_be_set,
                         uint32_t timeout_us) {
  prv_set_num_data_bytes(1);

  QSPI_AutoPollingMode_SetInterval(dev->auto_polling_interval);
  QSPI_AutoPollingMode_Config(should_be_set ? bit_mask : 0, bit_mask, QSPI_PMM_AND);
  QSPI_AutoPollingModeStopCmd(ENABLE);

  prv_set_sdr();
  prv_set_comm_config(QSPI_ComConfig_FMode_Auto_Polling | QSPI_ComConfig_DMode_4Line |
                          QSPI_ComConfig_IMode_4Line | instruction,
                      0);

  uint32_t loops = 0;
  while (QSPI_GetFlagStatus(QSPI_FLAG_SM) == RESET) {
    if ((timeout_us != QSPI_NO_TIMEOUT) && (++loops > timeout_us)) {
      QSPI_AbortRequest();
      prv_wait_for_not_busy();
      PBL_LOG_ERR("Timeout polling QSPI flash status");
      return false;
    }
    pbl_busy_wait_us(1);
  }

  QSPI_AbortRequest();
  prv_wait_for_not_busy();

  return true;
}

// QSPI flash
///////////////////////////////////////////////////////////////////////////////

static void prv_read_register(QSPIFlash *dev, uint8_t instruction, uint8_t *data, uint32_t length) {
  prv_indirect_read(instruction, QSPI_ADDR_NO_ADDR, 0, data, length);
}

static void prv_write_cmd_no_addr(QSPIFlash *dev, uint8_t cmd) {
  prv_indirect_write(cmd, QSPI_ADDR_NO_ADDR, nullptr, 0);
}

static void prv_write_enable(QSPIFlash *dev) {
  prv_write_cmd_no_addr(dev, dev->state->part->instructions.write_enable);
  prv_poll_bit(dev->qspi, dev->state->part->instructions.rdsr1,
               dev->state->part->status_bit_masks.write_enable, true, QSPI_NO_TIMEOUT);
}

static bool prv_check_whoami(QSPIFlash *dev) {
  uint32_t read_whoami = 0;
  prv_read_register(dev, dev->state->part->instructions.qspi_id, (uint8_t *)&read_whoami, 3);

  if (read_whoami == dev->state->part->qspi_id_value) {
    PBL_LOG_INFO("Flash is %s", dev->state->part->name);
    return true;
  }

  PBL_LOG_ERR("Flash isn't expected %s (whoami: 0x%" PRIx32 ")", dev->state->part->name,
              read_whoami);
  return false;
}

bool qspi_flash_check_whoami(QSPIFlash *dev) {
  return prv_check_whoami(dev);
}

bool qspi_flash_is_in_coredump_mode(QSPIFlash *dev) {
  return dev->state->coredump_mode;
}

void qspi_flash_init(QSPIFlash *dev, QSPIFlashPart *part, bool coredump_mode) {
  dev->state->part = part;
  dev->state->coredump_mode = coredump_mode;
  dev->state->fast_read_ddr_enabled = false;

  prv_port_init(dev->qspi);

  // The flash may be in SPI or QPI mode. Enter QPI (a no-op if already
  // there), reset to abort any program or erase from before the reboot
  // (which drops back to SPI), then enter QPI again.
  prv_indirect_write_no_addr_1line(part->instructions.enter_quad_mode);
  prv_write_cmd_no_addr(dev, part->instructions.reset_enable);
  prv_write_cmd_no_addr(dev, part->instructions.reset);

  if (coredump_mode) {
    pbl_busy_wait_us(part->reset_latency_ms * 1000);
  } else {
    psleep(part->reset_latency_ms);
  }

  prv_indirect_write_no_addr_1line(part->instructions.enter_quad_mode);

  if (!coredump_mode) {
    prv_check_whoami(dev);
  }
}

status_t qspi_flash_is_erase_complete(QSPIFlash *dev) {
  uint8_t status_reg;
  uint8_t flag_status_reg;
  prv_read_register(dev, dev->state->part->instructions.rdsr1, &status_reg, 1);
  prv_read_register(dev, dev->state->part->instructions.rdsr2, &flag_status_reg, 1);

  if (status_reg & dev->state->part->status_bit_masks.busy) {
    return E_BUSY;
  } else if (flag_status_reg & dev->state->part->flag_status_bit_masks.erase_suspend) {
    return E_AGAIN;
  }
  return S_SUCCESS;
}

status_t qspi_flash_erase_begin(QSPIFlash *dev, uint32_t addr, bool is_subsector) {
  const uint8_t instruction = is_subsector ? dev->state->part->instructions.erase_sector_4k
                                           : dev->state->part->instructions.erase_block_64k;

  prv_write_enable(dev);
  prv_indirect_write(instruction, addr, nullptr, 0);

  // Busy is set once the erase starts.
  const bool result = prv_poll_bit(dev->qspi, dev->state->part->instructions.rdsr1,
                                   dev->state->part->status_bit_masks.busy, true, 500);

  return result ? S_SUCCESS : E_ERROR;
}

status_t qspi_flash_erase_suspend(QSPIFlash *dev, uint32_t addr) {
  uint8_t status_reg;
  prv_read_register(dev, dev->state->part->instructions.rdsr1, &status_reg, 1);
  if (!(status_reg & dev->state->part->status_bit_masks.busy)) {
    return S_NO_ACTION_REQUIRED;
  }

  prv_write_cmd_no_addr(dev, dev->state->part->instructions.erase_suspend);

  if (dev->state->part->suspend_to_read_latency_us) {
    pbl_busy_wait_us(dev->state->part->suspend_to_read_latency_us);
  }

  return S_SUCCESS;
}

void qspi_flash_erase_resume(QSPIFlash *dev, uint32_t addr) {
  prv_write_cmd_no_addr(dev, dev->state->part->instructions.erase_resume);
  prv_poll_bit(dev->qspi, dev->state->part->instructions.rdsr2,
               dev->state->part->flag_status_bit_masks.erase_suspend, false, QSPI_NO_TIMEOUT);
}

void qspi_flash_read_blocking(QSPIFlash *dev, uint32_t addr, void *buffer, uint32_t length) {
  prv_indirect_read(dev->state->part->instructions.fast_read, addr,
                    dev->state->part->dummy_cycles.fast_read, buffer, length);
}

int qspi_flash_write_page_begin(QSPIFlash *dev, const void *buffer, uint32_t addr,
                                uint32_t length) {
  const uint32_t offset_in_page = addr % PAGE_SIZE_BYTES;
  const uint32_t bytes_in_page = MIN(PAGE_SIZE_BYTES - offset_in_page, length);

  prv_write_enable(dev);
  prv_indirect_write(dev->state->part->instructions.pp, addr, buffer, bytes_in_page);
  prv_poll_bit(dev->qspi, dev->state->part->instructions.rdsr1,
               dev->state->part->status_bit_masks.busy, false, QSPI_NO_TIMEOUT);

  return bytes_in_page;
}

status_t qspi_flash_get_write_status(QSPIFlash *dev) {
  uint8_t status_reg;
  prv_read_register(dev, dev->state->part->instructions.rdsr1, &status_reg, 1);
  return (status_reg & dev->state->part->status_bit_masks.busy) ? E_BUSY : S_SUCCESS;
}

void qspi_flash_set_lower_power_mode(QSPIFlash *dev, bool active) {
  uint8_t instruction;
  uint32_t delay;
  if (active) {
    instruction = dev->state->part->instructions.enter_low_power;
    delay = dev->state->part->standby_to_low_power_latency_us;
  } else {
    instruction = dev->state->part->instructions.exit_low_power;
    delay = dev->state->part->low_power_to_standby_latency_us;
  }
  prv_write_cmd_no_addr(dev, instruction);
  if (delay) {
    pbl_busy_wait_us(delay);
  }
}

status_t qspi_flash_blank_check(QSPIFlash *dev, uint32_t addr, bool is_subsector) {
  const uint32_t size_bytes = is_subsector ? SUBSECTOR_SIZE_BYTES : SECTOR_SIZE_BYTES;
  uint32_t buffer[32];

  for (uint32_t offset = 0; offset < size_bytes; offset += sizeof(buffer)) {
    qspi_flash_read_blocking(dev, addr + offset, buffer, sizeof(buffer));
    for (uint32_t i = 0; i < ARRAY_LENGTH(buffer); ++i) {
      if (buffer[i] != FLASH_RESET_WORD_VALUE) {
        return S_FALSE;
      }
    }
  }

  return S_TRUE;
}

// Block protection
///////////////////////////////////////////////////////////////////////////////

bool qspi_flash_protection_is_enabled(QSPIFlash *dev) {
  uint8_t status;
  prv_read_register(dev, dev->state->part->instructions.read_protection_status, &status, 1);
  return (status & dev->state->part->block_lock.protection_enabled_mask);
}

// Enabling the flash's write protection mode is irreversible (an OTP bit on
// MX25U), so this firmware never does it. It only uses block locks when the
// mode is already enabled.
status_t qspi_flash_write_protection_enable(QSPIFlash *dev) {
  return qspi_flash_protection_is_enabled(dev) ? S_NO_ACTION_REQUIRED : E_INVALID_OPERATION;
}

status_t qspi_flash_lock_sector(QSPIFlash *dev, uint32_t addr) {
  prv_write_enable(dev);

  const uint8_t instruction = dev->state->part->instructions.block_lock;
  if (dev->state->part->block_lock.has_lock_data) {
    prv_indirect_write(instruction, addr, &dev->state->part->block_lock.lock_data, 1);
  } else {
    prv_indirect_write(instruction, addr, nullptr, 0);
  }

  prv_poll_bit(dev->qspi, dev->state->part->instructions.rdsr1,
               dev->state->part->status_bit_masks.busy, false, QSPI_NO_TIMEOUT);

  uint8_t status;
  prv_indirect_read(dev->state->part->instructions.block_lock_status, addr, 0, &status,
                    sizeof(status));

  return (status == dev->state->part->block_lock.locked_check) ? S_SUCCESS : E_ERROR;
}

status_t qspi_flash_unlock_all(QSPIFlash *dev) {
  prv_write_enable(dev);
  prv_write_cmd_no_addr(dev, dev->state->part->instructions.block_unlock_all);
  return S_SUCCESS;
}

// Security registers: not used on this hardware.
///////////////////////////////////////////////////////////////////////////////

status_t qspi_flash_read_security_register(QSPIFlash *dev, uint32_t addr, uint8_t *val) {
  return E_INVALID_OPERATION;
}

status_t qspi_flash_security_register_is_locked(QSPIFlash *dev, uint32_t address, bool *locked) {
  return E_INVALID_OPERATION;
}

status_t qspi_flash_erase_security_register(QSPIFlash *dev, uint32_t addr) {
  return E_INVALID_OPERATION;
}

status_t qspi_flash_write_security_register(QSPIFlash *dev, uint32_t addr, uint8_t val) {
  return E_INVALID_OPERATION;
}

const FlashSecurityRegisters *qspi_flash_security_registers_info(QSPIFlash *dev) {
  return &dev->state->part->sec_registers;
}

status_t qspi_flash_lock_security_register(QSPIFlash *dev, uint32_t address) {
  return E_INVALID_OPERATION;
}

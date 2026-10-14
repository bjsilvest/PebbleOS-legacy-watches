/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/gpio.h>
#include <pbl/kernel/sem.h>

#include <board/board.h>

#ifdef CONFIG_SOC_SF32LB52
#include <bf0_hal_dma.h>
#include <bf0_hal_mpi.h>
#endif

/**
 * @defgroup drivers_qspi_definitions QSPI port
 * @ingroup drivers
 * @brief Board description of a QSPI port.
 *
 * The fields depend on the SoC.
 * @{
 */

/** @brief Number of QSPI data lines. */
#define QSPI_NUM_DATA_PINS (4)

/** @brief QSPI port state, owned by the driver. */
typedef struct QSPIPortState {
#ifdef CONFIG_SOC_NRF52
  /** Serializes transfers. */
  struct pbl_sem sem;
  /** Port initialized. */
  bool initialized;
#elifdef CONFIG_SOC_SF32LB52
  /** HAL flash context. */
  QSPI_FLASH_CTX_T ctx;
  /** DMA handle. */
  DMA_HandleTypeDef hdma;
  /** HAL controller configuration. */
  qspi_configure_t cfg;
  /** DMA configuration. */
  struct dma_config dma;
  /** Time in microseconds to enter deep power-down. */
  uint32_t t_enter_deep_us;
  /** Time in microseconds to exit deep power-down. */
  uint32_t t_exit_deep_us;
  /** Port initialized. */
  bool initialized;
#elifdef CONFIG_SOC_STM32F4
  /** Serializes transfers. */
  struct pbl_sem sem;
  /** Port initialized. */
  bool initialized;
#else
  /** Signals DMA completion. */
  struct pbl_sem dma_semaphore;
  /** Number of qspi_use() references. */
  int use_count;
#endif
} QSPIPortState;

/** @brief QSPI port board description. */
typedef const struct QSPIPort {
  /** Driver state. */
  QSPIPortState *state;
#ifdef CONFIG_SOC_NRF52
  /** Clock frequency in Hz. */
  uint32_t clk_freq_hz;
  /** Chip select pin. */
  uint32_t cs_gpio;
  /** Clock pin. */
  uint32_t clk_gpio;
  /** Data pins, IO0 to IO3. */
  uint32_t data_gpio[QSPI_NUM_DATA_PINS];
#elifdef CONFIG_SOC_SF32LB52
  /** Clock divider. */
  uint16_t clk_div;
#elifdef CONFIG_SOC_STM32F4
  /** Maximum clock frequency in Hz. */
  uint32_t clock_speed_hz;
  /** Cycles between status reads when auto-polling. */
  uint16_t auto_polling_interval;
  /** Chip select pin and its alternate function. */
  struct pbl_gpio cs_gpio;
  uint8_t cs_af;
  /** Clock pin and its alternate function. */
  struct pbl_gpio clk_gpio;
  uint8_t clk_af;
  /** Data pins, IO0 to IO3, and their alternate function. */
  struct pbl_gpio data_gpio[QSPI_NUM_DATA_PINS];
  uint8_t data_af;
  /** Flash size in bytes, a power of two. */
  uint32_t flash_size;
#endif
} QSPIPort;

/** @} */

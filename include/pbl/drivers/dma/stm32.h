/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/kernel/irq.h>

#include <stm32f4xx.h>

/** @brief Called from the DMA interrupt for each half of a circular buffer. */
typedef void (*PblDmaStm32Callback)(void *context, bool is_complete);

struct pbl_dma_stm32_state {
  PblDmaStm32Callback cb;
  void *context;
};

/** @brief One DMA stream, used for a circular peripheral-to-memory transfer. */
struct pbl_dma_stm32_stream {
  struct pbl_dma_stm32_state *state;
  DMA_Stream_TypeDef *stream;
  /** Request channel (DMA_Channel_x). */
  uint32_t channel;
  /** RCC_AHB1Periph_DMAx. */
  uint32_t rcc;
  /** Half and full transfer interrupt flags of this stream (DMA_IT_HTIFx, DMA_IT_TCIFx). */
  uint32_t it_ht;
  uint32_t it_tc;
  pbl_irq_t irqn;
};

/**
 * @brief Start a circular transfer of 32-bit words from @p periph_addr into
 * @p buffer, calling @p cb as each half fills.
 */
void pbl_dma_stm32_start_circular(const struct pbl_dma_stm32_stream *dma,
                                  volatile void *periph_addr, void *buffer, uint32_t size_bytes,
                                  uint32_t priority, PblDmaStm32Callback cb, void *context);

/** @brief Stop the transfer. */
/** Like pbl_dma_stm32_start_circular(), but moving bytes (e.g. from a UART). */
void pbl_dma_stm32_start_circular_u8(const struct pbl_dma_stm32_stream *dma,
                                     volatile void *periph_addr, void *buffer, uint32_t size_bytes,
                                     uint32_t priority, PblDmaStm32Callback cb, void *context);

/** Like pbl_dma_stm32_start_circular(), but moving half-words (e.g. from I2S). */
void pbl_dma_stm32_start_circular_u16(const struct pbl_dma_stm32_stream *dma,
                                      volatile void *periph_addr, void *buffer, uint32_t size_bytes,
                                      uint32_t priority, PblDmaStm32Callback cb, void *context);

/**
 * One-shot memory-to-peripheral transfer of bytes. @p cb is called with
 * is_complete true when the last byte has been handed to the peripheral.
 */
void pbl_dma_stm32_start_tx(const struct pbl_dma_stm32_stream *dma, volatile void *periph_addr,
                            const void *buffer, uint32_t size_bytes, uint32_t priority,
                            PblDmaStm32Callback cb, void *context);

/** Transfers left before the circular buffer wraps (the NDTR register). */
uint32_t pbl_dma_stm32_remaining(const struct pbl_dma_stm32_stream *dma);

void pbl_dma_stm32_stop(const struct pbl_dma_stm32_stream *dma);

/** @brief Stream interrupt handler, connected with PBL_IRQ_CONNECT() in the board file. */
void pbl_dma_stm32_irq_handler(const struct pbl_dma_stm32_stream *dma);

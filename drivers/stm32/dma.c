/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/dma/stm32.h>

#include <system/passert.h>

static void prv_start_circular(const struct pbl_dma_stm32_stream *dma, volatile void *periph_addr,
                               void *buffer, uint32_t size_bytes, uint32_t unit_bytes,
                               uint32_t priority, PblDmaStm32Callback cb, void *context) {
  PBL_ASSERTN((size_bytes % unit_bytes) == 0);

  dma->state->cb = cb;
  dma->state->context = context;

  RCC_AHB1PeriphClockCmd(dma->rcc, ENABLE);

  DMA_Cmd(dma->stream, DISABLE);
  while (DMA_GetCmdStatus(dma->stream) != DISABLE) {
  }
  DMA_DeInit(dma->stream);

  DMA_InitTypeDef init;
  DMA_StructInit(&init);
  init.DMA_Channel = dma->channel;
  init.DMA_PeripheralBaseAddr = (uint32_t)(uintptr_t)periph_addr;
  init.DMA_Memory0BaseAddr = (uint32_t)(uintptr_t)buffer;
  init.DMA_DIR = DMA_DIR_PeripheralToMemory;
  init.DMA_BufferSize = size_bytes / unit_bytes;
  init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  init.DMA_PeripheralDataSize = (unit_bytes == 4)   ? DMA_PeripheralDataSize_Word
                                : (unit_bytes == 2) ? DMA_PeripheralDataSize_HalfWord
                                                    : DMA_PeripheralDataSize_Byte;
  init.DMA_MemoryDataSize = (unit_bytes == 4)   ? DMA_MemoryDataSize_Word
                            : (unit_bytes == 2) ? DMA_MemoryDataSize_HalfWord
                                                : DMA_MemoryDataSize_Byte;
  init.DMA_Mode = DMA_Mode_Circular;
  init.DMA_Priority = priority;
  init.DMA_FIFOMode = DMA_FIFOMode_Disable;
  DMA_Init(dma->stream, &init);

  DMA_ClearITPendingBit(dma->stream, dma->it_ht | dma->it_tc);
  DMA_ITConfig(dma->stream, DMA_IT_HT | DMA_IT_TC, ENABLE);
  pbl_irq_enable(dma->irqn);
  DMA_Cmd(dma->stream, ENABLE);
}

void pbl_dma_stm32_start_circular(const struct pbl_dma_stm32_stream *dma,
                                  volatile void *periph_addr, void *buffer, uint32_t size_bytes,
                                  uint32_t priority, PblDmaStm32Callback cb, void *context) {
  prv_start_circular(dma, periph_addr, buffer, size_bytes, 4, priority, cb, context);
}

void pbl_dma_stm32_start_circular_u8(const struct pbl_dma_stm32_stream *dma,
                                     volatile void *periph_addr, void *buffer, uint32_t size_bytes,
                                     uint32_t priority, PblDmaStm32Callback cb, void *context) {
  prv_start_circular(dma, periph_addr, buffer, size_bytes, 1, priority, cb, context);
}

void pbl_dma_stm32_start_circular_u16(const struct pbl_dma_stm32_stream *dma,
                                      volatile void *periph_addr, void *buffer, uint32_t size_bytes,
                                      uint32_t priority, PblDmaStm32Callback cb, void *context) {
  prv_start_circular(dma, periph_addr, buffer, size_bytes, 2, priority, cb, context);
}

void pbl_dma_stm32_start_tx(const struct pbl_dma_stm32_stream *dma, volatile void *periph_addr,
                            const void *buffer, uint32_t size_bytes, uint32_t priority,
                            PblDmaStm32Callback cb, void *context) {
  dma->state->cb = cb;
  dma->state->context = context;

  RCC_AHB1PeriphClockCmd(dma->rcc, ENABLE);

  DMA_Cmd(dma->stream, DISABLE);
  while (DMA_GetCmdStatus(dma->stream) != DISABLE) {
  }
  DMA_DeInit(dma->stream);

  DMA_InitTypeDef init;
  DMA_StructInit(&init);
  init.DMA_Channel = dma->channel;
  init.DMA_PeripheralBaseAddr = (uint32_t)(uintptr_t)periph_addr;
  init.DMA_Memory0BaseAddr = (uint32_t)(uintptr_t)buffer;
  init.DMA_DIR = DMA_DIR_MemoryToPeripheral;
  init.DMA_BufferSize = size_bytes;
  init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  init.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  init.DMA_Mode = DMA_Mode_Normal;
  init.DMA_Priority = priority;
  init.DMA_FIFOMode = DMA_FIFOMode_Disable;
  DMA_Init(dma->stream, &init);

  DMA_ClearITPendingBit(dma->stream, dma->it_ht | dma->it_tc);
  DMA_ITConfig(dma->stream, DMA_IT_TC, ENABLE);
  pbl_irq_enable(dma->irqn);
  DMA_Cmd(dma->stream, ENABLE);
}

uint32_t pbl_dma_stm32_remaining(const struct pbl_dma_stm32_stream *dma) {
  return DMA_GetCurrDataCounter(dma->stream);
}

void pbl_dma_stm32_stop(const struct pbl_dma_stm32_stream *dma) {
  pbl_irq_disable(dma->irqn);
  DMA_ITConfig(dma->stream, DMA_IT_HT | DMA_IT_TC, DISABLE);
  DMA_Cmd(dma->stream, DISABLE);
  while (DMA_GetCmdStatus(dma->stream) != DISABLE) {
  }
  DMA_ClearITPendingBit(dma->stream, dma->it_ht | dma->it_tc);
  dma->state->cb = nullptr;
}

void pbl_dma_stm32_irq_handler(const struct pbl_dma_stm32_stream *dma) {
  if (DMA_GetITStatus(dma->stream, dma->it_ht) != RESET) {
    DMA_ClearITPendingBit(dma->stream, dma->it_ht);
    if (dma->state->cb) {
      dma->state->cb(dma->state->context, false);
    }
  }
  if (DMA_GetITStatus(dma->stream, dma->it_tc) != RESET) {
    DMA_ClearITPendingBit(dma->stream, dma->it_tc);
    if (dma->state->cb) {
      dma->state->cb(dma->state->context, true);
    }
  }
}

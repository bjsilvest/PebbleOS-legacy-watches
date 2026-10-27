/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/drivers/dma/stm32.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/mic.h>
#include <pbl/kernel/mutex.h>
#include <pbl/util/circular_buffer.h>

#include <OpenPDMFilter.h>
#include <stm32f4xx.h>

#define I2S_PDM_DECIMATION (64)
#define I2S_PDM_BATCH_SIZE (8)
//! Two 1K halves, each filling in about 1/64 s.
#define I2S_PDM_BUFFER_LENGTH    (I2S_PDM_DECIMATION * I2S_PDM_BATCH_SIZE)
#define I2S_PDM_CIRC_BUFFER_SIZE ((MIC_SAMPLE_RATE / 1000) * 10 * 4 * sizeof(uint16_t))

typedef struct MicState {
  uint16_t in_buffer[2][I2S_PDM_BUFFER_LENGTH];     // 2k
  uint8_t circ_buf_store[I2S_PDM_CIRC_BUFFER_SIZE]; // 1.25k
  CircularBuffer circ_buffer;
  TPDMFilter_InitStruct pdm_filter;
  bool initialized;
  bool running;
  bool bg_pending;
  uint32_t overflow_cnt;
  //! Milliseconds of output discarded while the filter settles.
  uint8_t discarded_ms;
  uint16_t volume;
  // Keeps mic_stop() from freeing the subscriber's buffer while the dispatch
  // routine is still using it.
  struct pbl_mutex mic_mutex;
  struct MicSubscriber {
    MicDataHandlerCB callback;
    int16_t *buffer;
    void *context;
    size_t size;
    size_t idx;
  } subscriber;
} MicDeviceState;

//! PDM microphone sampled by an SPI in I2S master receive mode, with the
//! OpenPDMFilter decimating to 16 kHz.
typedef const struct MicDevice {
  MicDeviceState *state;
  SPI_TypeDef *spi;
  uint32_t spi_rcc; ///< RCC_APB1Periph_SPIx
  const struct pbl_dma_stm32_stream *dma;
  struct pbl_gpio ck_gpio;
  struct pbl_gpio sd_gpio;
  uint8_t gpio_af;
  //! OpenPDMFilter gain (stock: 250)
  uint16_t default_volume;
} MicDevice;

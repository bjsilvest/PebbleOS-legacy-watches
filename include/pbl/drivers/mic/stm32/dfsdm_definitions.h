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

#include <stm32f4xx.h>

#define DFSDM_BUFFER_LENGTH    (256) // Each half fills 64 times per second
#define DFSDM_CIRC_BUFFER_SIZE (DFSDM_BUFFER_LENGTH * sizeof(uint16_t) * 2)

typedef struct MicState {
  int32_t in_buffer[2][DFSDM_BUFFER_LENGTH];      // 2k
  uint8_t circ_buf_store[DFSDM_CIRC_BUFFER_SIZE]; // 1k
  CircularBuffer circ_buffer;
  bool initialized;
  bool running;
  bool bg_pending;
  uint32_t overflow_cnt;
  uint32_t bytes_received;
  int samples_to_discard;
  int64_t hpf_y1; // Previous value of HPF output
  int16_t prev_r; // Previous random number generated for dithering
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

typedef const struct MicDevice {
  MicDeviceState *state;
  DFSDM_TypeDef *filter;
  DFSDM_Channel_TypeDef *channel;
  uint32_t regular_channel;
  uint32_t pdm_frequency;
  uint32_t rcc_apb_periph;
  const struct pbl_dma_stm32_stream *dma;
  struct pbl_gpio ck_gpio;
  struct pbl_gpio sd_gpio;
  uint8_t gpio_af;
  int power_on_delay_ms;
  int settling_delay_ms;
  //! Volume scalar (max 256)
  uint16_t default_volume;
  //! Final right shift after gain: a volume of 128 gives about half of full
  //! scale for normal speech.
  int final_right_shift;
} MicDevice;

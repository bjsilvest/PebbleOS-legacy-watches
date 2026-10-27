/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! PDM microphone through the STM32F412 DFSDM, as the stock silk firmware
//! did it: sinc filter, 10 Hz high-pass, volume, TPDF dither, 16 kHz output.

#include <inttypes.h>

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/mic.h>
#include <pbl/drivers/mic/stm32/dfsdm_definitions.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/fatal.h>
#include <pbl/logging/logging.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/stm32/sleep.h>
#include <pbl/util/math.h>


PBL_LOG_MODULE_DEFINE(driver_mic, CONFIG_DRIVER_MIC_LOG_LEVEL);

#define MAX_VOLUME (256)
#define LFSR_SEED  (0x3AEF)

#define DFSDM_DMA_PRIORITY DMA_Priority_VeryHigh

static void prv_dma_handler(void *context, bool is_complete);

static void prv_enable_gpio(MicDevice *this) {
  pbl_gpio_stm32_af(&this->ck_gpio, this->gpio_af, 0, GPIO_Medium_Speed);
  pbl_gpio_stm32_af(&this->sd_gpio, this->gpio_af, 0, GPIO_Medium_Speed);
}

static void prv_disable_gpio(MicDevice *this) {
  // Clock held low, data floating in analog mode, for the lowest power.
  pbl_gpio_configure(&this->ck_gpio, PBL_GPIO_OUTPUT_LOW);
  pbl_gpio_stm32_analog(&this->sd_gpio);
}

static void prv_dfsdm_configure(MicDevice *this) {
  const uint32_t k_max_sinc4_osr = 255;

  PBL_ASSERTN(this->pdm_frequency > 0);
  PBL_ASSERTN(this->pdm_frequency % MIC_SAMPLE_RATE == 0);
  const uint32_t oversampling_ratio = this->pdm_frequency / MIC_SAMPLE_RATE;
  const uint32_t sinc_order =
      (oversampling_ratio <= k_max_sinc4_osr) ? DFSDM_SincOrder_Sinc4 : DFSDM_SincOrder_Sinc3;

  // Keep the filter output within 24 bits.
  int num_bits = ceil_log_two(oversampling_ratio);
  num_bits *= (sinc_order == DFSDM_SincOrder_Sinc4) ? 4 : 3;
  const uint32_t right_shift = MAX(num_bits - 24, 0);

  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);
  const uint32_t prescaler = clocks.PCLK2_Frequency / this->pdm_frequency;

  DFSDM_Command(DISABLE);
  DFSDM_ChannelCmd(this->channel, DISABLE);
  DFSDM_FilterCmd(this->filter, DISABLE);

  DFSDM_TransceiverInitTypeDef transceiver;
  DFSDM_TransceiverStructInit(&transceiver);
  transceiver.DFSDM_Interface = DFSDM_Interface_SPI_RisingEdge;
  transceiver.DFSDM_Clock = DFSDM_Clock_Internal;
  transceiver.DFSDM_Input = DFSDM_Input_External;
  transceiver.DFSDM_Redirection = DFSDM_Redirection_Disabled;
  transceiver.DFSDM_PackingMode = DFSDM_PackingMode_Standard;
  transceiver.DFSDM_DataRightShift = right_shift;
  transceiver.DFSDM_Offset = 0;
  transceiver.DFSDM_CLKAbsenceDetector = DFSDM_CLKAbsenceDetector_Disable;
  transceiver.DFSDM_ShortCircuitDetector = DFSDM_ShortCircuitDetector_Enable;
  DFSDM_TransceiverInit(this->channel, &transceiver);

  DFSDM_FilterInitTypeDef filter;
  DFSDM_FilterStructInit(&filter);
  filter.DFSDM_SincOrder = sinc_order;
  filter.DFSDM_FilterOversamplingRatio = oversampling_ratio;
  filter.DFSDM_IntegratorOversamplingRatio = 1;
  DFSDM_FilterInit(this->filter, &filter);

  DFSDM_ConfigClkOutputSource(DFSDM_ClkOutSource_SysClock);
  DFSDM_ConfigClkOutputDivider(prescaler);
  DFSDM_SelectRegularChannel(this->filter, this->regular_channel);
  DFSDM_FastModeCmd(this->filter, ENABLE);
}

static void prv_dfsdm_enable(MicDevice *this) {
  DFSDM_Command(ENABLE);
  DFSDM_ChannelCmd(this->channel, ENABLE);
  pbl_sleep_ms(this->power_on_delay_ms);

  DFSDM_DMATransferConfig(this->filter, DFSDM_DMAConversionMode_Regular, ENABLE);
  DFSDM_FilterCmd(this->filter, ENABLE);
  DFSDM_RegularContinuousModeCmd(this->filter, ENABLE);
  DFSDM_StartSoftwareRegularConversion(this->filter);

  pbl_dma_stm32_start_circular(this->dma, &this->filter->FLTRDATAR, this->state->in_buffer,
                               sizeof(this->state->in_buffer), DFSDM_DMA_PRIORITY, prv_dma_handler,
                               (void *)this);
}

static void prv_dfsdm_disable(MicDevice *this) {
  pbl_dma_stm32_stop(this->dma);
  DFSDM_ChannelCmd(this->channel, DISABLE);
  DFSDM_Command(DISABLE);
  RCC_APB2PeriphClockCmd(this->rcc_apb_periph, DISABLE);
}

void mic_init(MicDevice *this) {
  PBL_ASSERTN(!this->state->initialized);

  this->state->bg_pending = false;
  this->state->running = false;
  this->state->subscriber = (struct MicSubscriber){0};
  this->state->volume = this->default_volume;

  prv_disable_gpio(this);
  pbl_mutex_init(&this->state->mic_mutex);

  this->state->initialized = true;
}

void mic_set_volume(MicDevice *this, uint16_t volume) {
  this->state->volume = MIN(MAX_VOLUME, volume);
}

bool mic_start(MicDevice *this, MicDataHandlerCB data_handler, void *context, int16_t *audio_buffer,
               size_t audio_buffer_len) {
  PBL_ASSERTN(this->state->initialized);

  pbl_mutex_lock(&this->state->mic_mutex, PBL_FOREVER);

  bool success = false;
  if (this->state->running) {
    goto unlock;
  }

  circular_buffer_init(&this->state->circ_buffer, this->state->circ_buf_store,
                       DFSDM_CIRC_BUFFER_SIZE);

  this->state->subscriber = (struct MicSubscriber){
    .callback = data_handler,
    .buffer = audio_buffer,
    .context = context,
    .size = audio_buffer_len,
    .idx = 0,
  };
  this->state->overflow_cnt = 0;
  this->state->bytes_received = 0;
  this->state->samples_to_discard = (MIC_SAMPLE_RATE * this->settling_delay_ms) / 1000;
  this->state->hpf_y1 = 0;
  this->state->prev_r = LFSR_SEED;

  // The DFSDM and its DMA stop in STOP mode.
  soc_stm32_stop_block();

  prv_enable_gpio(this);
  RCC_APB2PeriphClockCmd(this->rcc_apb_periph, ENABLE);
  prv_dfsdm_configure(this);
  prv_dfsdm_enable(this);

  this->state->running = true;
  success = true;

unlock:
  pbl_mutex_unlock(&this->state->mic_mutex);
  return success;
}

bool mic_start_polling(MicDevice *this, MicDataHandlerCB data_handler, void *context,
                       int16_t *audio_buffer, size_t audio_buffer_len, MicDataReadyCB ready) {
  return false;
}

void mic_poll(MicDevice *this) {
}

bool mic_get_frame_time(MicDevice *this, uint32_t *sample_time) {
  return false;
}

void mic_stop(MicDevice *this) {
  pbl_mutex_lock(&this->state->mic_mutex, PBL_FOREVER);

  if (this->state->running) {
    prv_dfsdm_disable(this);
    prv_disable_gpio(this);
    soc_stm32_stop_release();
    this->state->running = false;

    PBL_LOG_DBG("Stopped microphone, dropped samples: %" PRIu32 " bytes received: %" PRIu32,
                this->state->overflow_cnt, this->state->bytes_received);
  }

  pbl_mutex_unlock(&this->state->mic_mutex);
}

bool mic_is_running(MicDevice *this) {
  return this->state->running;
}

uint32_t mic_get_channels(MicDevice *this) {
  return 1;
}

static void prv_dispatch_samples(void *context_ptr) {
  MicDevice *this = context_ptr;

  pbl_mutex_lock(&this->state->mic_mutex, PBL_FOREVER);
  while (this->state->running) {
    uint16_t size = circular_buffer_copy(
        &this->state->circ_buffer,
        (uint8_t *)&this->state->subscriber.buffer[this->state->subscriber.idx],
        ((this->state->subscriber.size - this->state->subscriber.idx) * sizeof(int16_t)));
    if (size == 0) {
      break;
    }

    this->state->subscriber.idx += (size / sizeof(int16_t));
    if (this->state->subscriber.idx == this->state->subscriber.size) {
      this->state->subscriber.callback(this->state->subscriber.buffer, this->state->subscriber.idx,
                                       this->state->subscriber.context);
      this->state->subscriber.idx = 0;
    }

    size -= size % sizeof(int16_t);
    circular_buffer_consume(&this->state->circ_buffer, size);
  }
  pbl_mutex_unlock(&this->state->mic_mutex);
}

static void prv_dispatch_samples_bg(void *context_ptr) {
  MicDevice *this = context_ptr;
  this->state->bg_pending = false;
  prv_dispatch_samples(context_ptr);
}

//! Galois LFSR, for dither.
static int16_t prv_get_lfsr(int16_t prev) {
  uint16_t lfsr = (uint16_t)prev;
  lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xB400);
  return (int16_t)lfsr;
}

static void prv_dma_handler(void *context, bool is_complete) {
  MicDevice *this = context;
  const int32_t *dfsdm_buffer = this->state->in_buffer[is_complete ? 1 : 0];

  if (this->state->bytes_received == 0) {
    // Seed the high-pass filter to avoid a startup transient.
    this->state->hpf_y1 = dfsdm_buffer[0] >> 8;
  }
  this->state->bytes_received += DFSDM_BUFFER_LENGTH * sizeof(*dfsdm_buffer);

  for (int i = 0; i < DFSDM_BUFFER_LENGTH; i++) {
    // The 24-bit sample is in the top of the 32-bit register.
    int32_t sample = dfsdm_buffer[i] >> 8;

    // Single-pole high-pass filter at 10 Hz to remove DC:
    // y[n] = a0 * x[n] + b1 * y[n-1], output x[n] - y[n].
    const int64_t b1 = 32639; // exp(-2 * PI * 10 / 16000), scaled by 2^15
    const int64_t a0 = 129;   // 1 - b1, scaled by 2^15
    const int64_t y = a0 * (int64_t)sample + b1 * this->state->hpf_y1;
    this->state->hpf_y1 = y >> 15;
    sample -= this->state->hpf_y1;

    sample *= this->state->volume;

    // Triangular PDF dither, 2 LSBs relative to the output.
    const int16_t r = prv_get_lfsr(this->state->prev_r);
    int16_t tpdf = (r / 2) + (this->state->prev_r / 2);
    this->state->prev_r = r;
    tpdf >>= this->final_right_shift - 1;
    sample += tpdf;

    sample >>= this->final_right_shift;
    sample = __SSAT(sample, 16);

    if (this->state->samples_to_discard > 0) {
      this->state->samples_to_discard--;
    } else if (!circular_buffer_write(&this->state->circ_buffer, (const uint8_t *)&sample,
                                      sizeof(int16_t))) {
      this->state->overflow_cnt++;
      break;
    }
  }

  // Encode on KernelBG, as the nRF5 and SF32LB52 mic drivers do, so that the
  // Speex encoder does not take CPU time from the UI on KernelMain.
  if (!this->state->bg_pending) {
    this->state->bg_pending = true;
    system_task_add_callback_from_isr(prv_dispatch_samples_bg, (void *)this);
  }
}

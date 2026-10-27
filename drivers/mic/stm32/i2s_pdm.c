/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! PDM microphone through an STM32F4 SPI in I2S master receive mode, as the
//! stock snowy firmware did it: 2.048 MHz PDM clock, OpenPDMFilter decimation
//! by 64 to 16 kHz. The mic is powered from the PMIC's LDO3.

#include <inttypes.h>

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/mic.h>
#include <pbl/drivers/mic/stm32/i2s_pdm_definitions.h>
#include <pbl/drivers/pmic.h>
#include <pbl/logging/logging.h>
#include <pbl/services/system_task.h>
#include <pbl/soc/stm32/sleep.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

#include <kernel/events.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

PBL_LOG_MODULE_DEFINE(driver_mic, CONFIG_DRIVER_MIC_LOG_LEVEL);

//! About 100 ms for the filter to settle.
#define MS_TO_SETTLE 100

static void prv_dma_handler(void *context, bool is_complete);

static void prv_enable_gpio(MicDevice *this) {
  pbl_gpio_stm32_af(&this->ck_gpio, this->gpio_af, 0, GPIO_High_Speed);
  pbl_gpio_stm32_af(&this->sd_gpio, this->gpio_af, 0, GPIO_High_Speed);
}

static void prv_disable_gpio(MicDevice *this) {
  // Clock held low, data floating in analog mode, for the lowest power.
  pbl_gpio_configure(&this->ck_gpio, PBL_GPIO_OUTPUT_LOW);
  pbl_gpio_stm32_analog(&this->sd_gpio);
}

static void prv_i2s_configure(MicDevice *this) {
  SPI_I2S_DeInit(this->spi);
  I2S_InitTypeDef i2s;
  I2S_StructInit(&i2s);
  i2s.I2S_AudioFreq = I2S_AudioFreq_32k;
  i2s.I2S_Standard = I2S_Standard_LSB;
  i2s.I2S_DataFormat = I2S_DataFormat_16b;
  i2s.I2S_CPOL = I2S_CPOL_High;
  i2s.I2S_Mode = I2S_Mode_MasterRx;
  i2s.I2S_MCLKOutput = I2S_MCLKOutput_Disable;
  I2S_Init(this->spi, &i2s);

  // The vendored I2S_Init() combines the HAL's packet length (16 bits, doubled
  // for the LSB standard) with the StdPeriph divisor of 32 * packetlength, so
  // here it computes a prescaler of 0 and falls back to 2: a ~9.6 MHz PDM clock
  // instead of ~1 MHz, and a filter ISR that never lets go of the CPU. Set the
  // prescaler for 32 bit clocks per 32 kHz frame from the PLLI2S output.
  const uint32_t pll_in = ((RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) ? HSE_VALUE : HSI_VALUE) /
                          (RCC->PLLCFGR & RCC_PLLCFGR_PLLM);
  const uint32_t plln = (RCC->PLLI2SCFGR & RCC_PLLI2SCFGR_PLLI2SN) >> 6;
  const uint32_t pllr = (RCC->PLLI2SCFGR & RCC_PLLI2SCFGR_PLLI2SR) >> 28;
  const uint32_t bit_clock = 32 * I2S_AudioFreq_32k;
  const uint32_t div = ((pll_in * plln) / pllr + bit_clock / 2) / bit_clock;
  PBL_ASSERTN(div >= 4 && div <= 0x1ff);
  this->spi->I2SPR = (uint16_t)((div / 2) | ((div & 1) << 8));
}

void mic_init(MicDevice *this) {
  PBL_ASSERTN(!this->state->initialized);

  this->state->main_pending = false;
  this->state->bg_pending = false;
  this->state->running = false;
  this->state->subscriber = (struct MicSubscriber){0};
  this->state->volume = this->default_volume;

  this->state->pdm_filter = (TPDMFilter_InitStruct){
    .Fs = MIC_SAMPLE_RATE,
    .LP_HZ = 8000,
    .HP_HZ = 10,
    .Out_MicChannels = 1,
    .In_MicChannels = 1,
    .Decimation = I2S_PDM_DECIMATION,
    .MaxVolume = 64,
  };

  prv_disable_gpio(this);
  pbl_mutex_init(&this->state->mic_mutex);

  this->state->initialized = true;
}

void mic_set_volume(MicDevice *this, uint16_t volume) {
  this->state->volume = volume;
}

bool mic_start(MicDevice *this, MicDataHandlerCB data_handler, void *context, int16_t *audio_buffer,
               size_t audio_buffer_len) {
  PBL_ASSERTN(this->state->initialized);

  pbl_mutex_lock(&this->state->mic_mutex, PBL_FOREVER);

  bool success = false;
  if (this->state->running) {
    goto unlock;
  }

  set_ldo3_power_state(true);

  circular_buffer_init(&this->state->circ_buffer, this->state->circ_buf_store,
                       I2S_PDM_CIRC_BUFFER_SIZE);
  this->state->subscriber = (struct MicSubscriber){
    .callback = data_handler,
    .buffer = audio_buffer,
    .context = context,
    .size = audio_buffer_len,
    .idx = 0,
  };
  this->state->overflow_cnt = 0;
  this->state->discarded_ms = 0;

  Open_PDM_Filter_Init(&this->state->pdm_filter);

  // The I2S, its PLL and the DMA stop in STOP mode.
  soc_stm32_stop_block();

  RCC_PLLI2SCmd(ENABLE);
  while (RCC_GetFlagStatus(RCC_FLAG_PLLI2SRDY) == RESET) {
  }

  prv_enable_gpio(this);
  RCC_APB1PeriphClockCmd(this->spi_rcc, ENABLE);
  prv_i2s_configure(this);
  SPI_I2S_DMACmd(this->spi, SPI_I2S_DMAReq_Rx, ENABLE);
  I2S_Cmd(this->spi, ENABLE);

  pbl_dma_stm32_start_circular_u16(this->dma, &this->spi->DR, this->state->in_buffer,
                                   sizeof(this->state->in_buffer), DMA_Priority_High,
                                   prv_dma_handler, (void *)this);

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
    pbl_dma_stm32_stop(this->dma);
    I2S_Cmd(this->spi, DISABLE);
    RCC_APB1PeriphClockCmd(this->spi_rcc, DISABLE);
    RCC_PLLI2SCmd(DISABLE);
    prv_disable_gpio(this);
    set_ldo3_power_state(false);
    soc_stm32_stop_release();
    this->state->running = false;

    PBL_LOG_DBG("Stopped microphone, dropped samples: %" PRIu32, this->state->overflow_cnt);
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

static void prv_dispatch_samples_main(void *context_ptr) {
  MicDevice *this = context_ptr;
  this->state->main_pending = false;
  prv_dispatch_samples(context_ptr);
}

static void prv_dispatch_samples_bg(void *context_ptr) {
  MicDevice *this = context_ptr;
  this->state->bg_pending = false;
  prv_dispatch_samples(context_ptr);
}

static void prv_dma_handler(void *context, bool is_complete) {
  MicDevice *this = context;
  uint16_t *pdm_buffer = this->state->in_buffer[is_complete ? 1 : 0];

  // The filter takes the bytes the other way round.
  for (size_t i = 0; i < I2S_PDM_BUFFER_LENGTH; i++) {
    pdm_buffer[i] = __REV16(pdm_buffer[i]);
  }

  bool overflow = false;
  uint16_t pcm16_buffer[MIC_SAMPLE_RATE / 1000]; // 1 ms
  const uint16_t *pdm_end = pdm_buffer + I2S_PDM_BUFFER_LENGTH;
  while (pdm_buffer < pdm_end) {
    Open_PDM_Filter_64((uint8_t *)pdm_buffer, pcm16_buffer, this->state->volume,
                       &this->state->pdm_filter);
    pdm_buffer += I2S_PDM_DECIMATION;

    if (this->state->discarded_ms < MS_TO_SETTLE) {
      this->state->discarded_ms++;
    } else if (!circular_buffer_write(&this->state->circ_buffer, (const uint8_t *)pcm16_buffer,
                                      sizeof(pcm16_buffer))) {
      overflow = true;
    }
  }
  if (overflow) {
    this->state->overflow_cnt++;
  }

  // Post to both KernelMain and KernelBG: whichever runs first empties the
  // buffer, so a busy KernelMain does not make it overrun.
  if (!this->state->main_pending) {
    this->state->main_pending = true;
    PebbleEvent e = {
      .type = PEBBLE_CALLBACK_EVENT,
      .callback = {
        .callback = prv_dispatch_samples_main,
        .data = (void *)this,
      },
    };
    event_put_isr(&e);
  }

  if (!this->state->bg_pending) {
    this->state->bg_pending = true;
    system_task_add_callback_from_isr(prv_dispatch_samples_bg, (void *)this);
  }
}

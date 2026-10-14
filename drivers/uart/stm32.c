/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F4 USART. RX is interrupt driven, or with uart_start_rx_dma() on a
//! device with an RX DMA stream, received into a circular buffer and handed
//! to the RX handler on line idle and at each half of the buffer.

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/uart.h>
#include <pbl/drivers/uart/stm32.h>
#include <pbl/kernel/irq.h>

#include <system/passert.h>

// Fixed point precision for the baud rate divider, as the ST library uses.
#define DIV_PRECISION (100)

typedef enum UARTCR1Flags {
  UARTCR1Flags_Duplex = USART_CR1_TE | USART_CR1_RE,
  UARTCR1Flags_TE = USART_CR1_TE,
  UARTCR1Flags_RE = USART_CR1_RE,
} UARTCR1Flags;

static void prv_clock(UARTDevice *dev, bool enable) {
  if (dev->apb2) {
    RCC_APB2PeriphClockCmd(dev->rcc_apb_periph, enable ? ENABLE : DISABLE);
  } else {
    RCC_APB1PeriphClockCmd(dev->rcc_apb_periph, enable ? ENABLE : DISABLE);
  }
}

static void prv_init(UARTDevice *dev, bool is_open_drain, UARTCR1Flags cr1_flags) {
  prv_clock(dev, true);

  const uint32_t flags = is_open_drain ? PBL_GPIO_OPEN_DRAIN : 0;
  if (pbl_gpio_is_connected(&dev->tx)) {
    pbl_gpio_stm32_af(&dev->tx, dev->af, flags, GPIO_Speed_50MHz);
  }
  if (pbl_gpio_is_connected(&dev->rx)) {
    pbl_gpio_stm32_af(&dev->rx, dev->af, flags, GPIO_Speed_50MHz);
  }

  // 8N1, no flow control
  dev->periph->CR1 = cr1_flags;
  dev->periph->CR2 = 0;
  dev->periph->CR3 = 0;

  // Clear stale errors: read SR then DR.
  (void)dev->periph->SR;
  (void)dev->periph->DR;

  dev->periph->CR1 |= USART_CR1_UE;

  dev->state->initialized = true;
}

void uart_init(UARTDevice *dev) {
  prv_init(dev, false, UARTCR1Flags_Duplex);
}

void uart_init_open_drain(UARTDevice *dev) {
  prv_init(dev, true, UARTCR1Flags_Duplex);
}

void uart_init_tx_only(UARTDevice *dev) {
  prv_init(dev, false, UARTCR1Flags_TE);
}

void uart_init_rx_only(UARTDevice *dev) {
  prv_init(dev, false, UARTCR1Flags_RE);
}

void uart_stop_rx_dma(UARTDevice *dev);

void uart_deinit(UARTDevice *dev) {
  uart_stop_rx_dma(dev);
  dev->periph->CR1 &= ~USART_CR1_UE;
  prv_clock(dev, false);

  // Digital inputs rather than analog: the pins may need to be 5V tolerant.
  if (pbl_gpio_is_connected(&dev->tx)) {
    pbl_gpio_configure(&dev->tx, PBL_GPIO_INPUT);
  }
  if (pbl_gpio_is_connected(&dev->rx)) {
    pbl_gpio_configure(&dev->rx, PBL_GPIO_INPUT);
  }
}

void uart_set_baud_rate(UARTDevice *dev, uint32_t baud_rate) {
  PBL_ASSERTN(dev->state->initialized);

  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);
  const uint64_t scaled_apbclock =
      (uint64_t)DIV_PRECISION * (dev->apb2 ? clocks.PCLK2_Frequency : clocks.PCLK1_Frequency);

  // BRR holds DIV = f_clk / (samples * baud) as fixed point, with as many
  // fractional bits as samples per symbol need (4 for 16x, 3 for 8x).
  const bool over8 = dev->periph->CR1 & USART_CR1_OVER8;
  const uint32_t samples = over8 ? 8 : 16;
  const uint32_t div_temp = scaled_apbclock / (samples * baud_rate);
  const uint32_t mantissa = div_temp / DIV_PRECISION;
  uint32_t fraction = div_temp - mantissa * DIV_PRECISION;
  fraction = (fraction * samples + (DIV_PRECISION / 2)) / DIV_PRECISION;

  if (over8) {
    dev->periph->BRR = (mantissa << 3) | (fraction & 0x7);
  } else {
    dev->periph->BRR = (mantissa << 4) | (fraction & 0xF);
  }
}

void uart_write_byte(UARTDevice *dev, uint8_t data) {
  PBL_ASSERTN(dev->state->initialized);

  while (!uart_is_tx_ready(dev)) {
  }

  dev->periph->DR = data;
}

uint8_t uart_read_byte(UARTDevice *dev) {
  // Reading DR also clears the error flags.
  return dev->periph->DR;
}

UARTRXErrorFlags uart_has_errored_out(UARTDevice *dev) {
  const uint16_t errors = dev->periph->SR;

  return (UARTRXErrorFlags){
    .parity_error = (errors & USART_FLAG_PE) != 0,
    .overrun_error = (errors & USART_FLAG_ORE) != 0,
    .framing_error = (errors & USART_FLAG_FE) != 0,
    .noise_detected = (errors & USART_FLAG_NE) != 0,
  };
}

bool uart_is_rx_ready(UARTDevice *dev) {
  return dev->periph->SR & USART_SR_RXNE;
}

bool uart_has_rx_overrun(UARTDevice *dev) {
  return dev->periph->SR & USART_SR_ORE;
}

bool uart_has_rx_framing_error(UARTDevice *dev) {
  return dev->periph->SR & USART_SR_FE;
}

bool uart_is_tx_ready(UARTDevice *dev) {
  return dev->periph->SR & USART_SR_TXE;
}

bool uart_is_tx_complete(UARTDevice *dev) {
  return dev->periph->SR & USART_SR_TC;
}

void uart_wait_for_tx_complete(UARTDevice *dev) {
  while (!uart_is_tx_complete(dev)) {
  }
}

// Interrupts
////////////////////////////////////////////////////////////////////////////////

static void prv_set_interrupt_enabled(UARTDevice *dev, bool enabled) {
  if (enabled) {
    PBL_ASSERTN(dev->state->tx_irq_handler || dev->state->rx_irq_handler);
    pbl_irq_enable(dev->irqn);
  } else {
    pbl_irq_disable(dev->irqn);
  }
}

void uart_set_rx_interrupt_handler(UARTDevice *dev, UARTRXInterruptHandler irq_handler) {
  PBL_ASSERTN(dev->state->initialized);
  dev->state->rx_irq_handler = irq_handler;
}

void uart_set_tx_interrupt_handler(UARTDevice *dev, UARTTXInterruptHandler irq_handler) {
  PBL_ASSERTN(dev->state->initialized);
  dev->state->tx_irq_handler = irq_handler;
}

static void prv_rx_dma_drain(UARTDevice *dev);

void uart_set_rx_interrupt_enabled(UARTDevice *dev, bool enabled) {
  PBL_ASSERTN(dev->state->initialized);
  if (dev->state->rx_dma_buffer) {
    // DMA keeps receiving; this only gates delivery to the handler.
    dev->state->rx_int_enabled = enabled;
    if (enabled) {
      pbl_irq_lock();
      prv_rx_dma_drain(dev);
      pbl_irq_unlock();
    }
    return;
  }
  if (enabled) {
    dev->state->rx_int_enabled = true;
    dev->periph->CR1 |= USART_CR1_RXNEIE;
    prv_set_interrupt_enabled(dev, true);
  } else {
    prv_set_interrupt_enabled(dev, dev->state->tx_int_enabled);
    dev->periph->CR1 &= ~USART_CR1_RXNEIE;
    dev->state->rx_int_enabled = false;
  }
}

void uart_set_tx_interrupt_enabled(UARTDevice *dev, bool enabled) {
  PBL_ASSERTN(dev->state->initialized);
  if (enabled) {
    dev->state->tx_int_enabled = true;
    dev->periph->CR1 |= USART_CR1_TXEIE;
    prv_set_interrupt_enabled(dev, true);
  } else {
    prv_set_interrupt_enabled(dev, dev->state->rx_int_enabled || dev->state->rx_dma_buffer);
    dev->periph->CR1 &= ~USART_CR1_TXEIE;
    dev->state->tx_int_enabled = false;
  }
}

void uart_irq_handler(UARTDevice *dev) {
  PBL_ASSERTN(dev->state->initialized);

  if (dev->state->rx_dma_buffer) {
    if (dev->periph->SR & USART_SR_IDLE) {
      // Reading SR then DR clears IDLE (and any overrun).
      (void)dev->periph->DR;
      prv_rx_dma_drain(dev);
    }
  } else if (dev->state->rx_irq_handler && dev->state->rx_int_enabled) {
    const UARTRXErrorFlags err_flags = {
      .overrun_error = uart_has_rx_overrun(dev),
      .framing_error = uart_has_rx_framing_error(dev),
    };
    const bool has_byte = uart_is_rx_ready(dev);
    // Read DR regardless, to clear the error flags.
    const uint8_t data = uart_read_byte(dev);
    if (has_byte) {
      dev->state->rx_irq_handler(dev, data, &err_flags);
    }
  }

  if (dev->state->tx_irq_handler && dev->state->tx_int_enabled && uart_is_tx_ready(dev)) {
    dev->state->tx_irq_handler(dev);
  }
}

void uart_clear_all_interrupt_flags(UARTDevice *dev) {
  dev->periph->SR &= ~(USART_SR_TXE | USART_SR_RXNE | USART_SR_ORE);
}

// DMA
////////////////////////////////////////////////////////////////////////////////

// Hand the bytes the DMA wrote since the last call to the RX handler. Called
// from the UART and DMA interrupts, or with interrupts locked.
static void prv_rx_dma_drain(UARTDevice *dev) {
  UARTDeviceState *state = dev->state;
  const UARTRXErrorFlags no_errors = {};

  if (!state->rx_dma_buffer || !state->rx_irq_handler) {
    return;
  }

  const uint32_t pos =
      (state->rx_dma_length - pbl_dma_stm32_remaining(dev->rx_dma)) % state->rx_dma_length;
  // The handler may pause delivery (rx_int_enabled) partway through.
  while (state->rx_int_enabled && state->rx_dma_index != pos) {
    const uint8_t data = state->rx_dma_buffer[state->rx_dma_index];
    state->rx_dma_index = (state->rx_dma_index + 1U) % state->rx_dma_length;
    state->rx_irq_handler(dev, data, &no_errors);
  }
}

static void prv_rx_dma_cb(void *context, bool is_complete) {
  prv_rx_dma_drain(context);
}

void uart_start_rx_dma(UARTDevice *dev, void *buffer, uint32_t length) {
  // Without a stream, RX stays interrupt driven, which callers handle the same way.
  if (!dev->rx_dma) {
    return;
  }

  PBL_ASSERTN(dev->state->initialized && dev->state->rx_irq_handler);

  pbl_irq_disable(dev->irqn);
  dev->periph->CR1 &= ~USART_CR1_RXNEIE;
  (void)dev->periph->SR;
  (void)dev->periph->DR;

  dev->state->rx_dma_buffer = buffer;
  dev->state->rx_dma_length = length;
  dev->state->rx_dma_index = 0U;

  pbl_dma_stm32_start_circular_u8(dev->rx_dma, &dev->periph->DR, buffer, length, DMA_Priority_High,
                                  prv_rx_dma_cb, (void *)dev);
  dev->periph->CR3 |= USART_CR3_DMAR;
  dev->periph->CR1 |= USART_CR1_IDLEIE;
  pbl_irq_enable(dev->irqn);
}

void uart_stop_rx_dma(UARTDevice *dev) {
  if (!dev->state->rx_dma_buffer) {
    return;
  }

  dev->periph->CR1 &= ~USART_CR1_IDLEIE;
  dev->periph->CR3 &= ~USART_CR3_DMAR;
  pbl_dma_stm32_stop(dev->rx_dma);
  dev->state->rx_dma_buffer = nullptr;
  prv_set_interrupt_enabled(dev, dev->state->tx_int_enabled);
}

void uart_clear_rx_dma_buffer(UARTDevice *dev) {
  if (!dev->state->rx_dma_buffer) {
    return;
  }

  pbl_irq_lock();
  dev->state->rx_dma_index = (dev->state->rx_dma_length - pbl_dma_stm32_remaining(dev->rx_dma)) %
                             dev->state->rx_dma_length;
  pbl_irq_unlock();
}

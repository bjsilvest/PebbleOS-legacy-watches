/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/dma/stm32.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/uart.h>

#include <board/board.h>
#include <stm32f4xx.h>

//! STM32F4 USART. Besides the @ref drivers_uart class, with PBL_UART_STM32_DEFINE(), a port has
//! the calls below, for the Bluetooth controllers' HCI: interrupt-driven transmission, RX DMA and
//! one-direction set-ups.

typedef struct UARTRXErrorFlags {
  union {
    struct {
      /** Reserved. */
      uint8_t reserved : 4;
      /** Parity error. */
      bool parity_error : 1;
      /** Receive overrun: data was lost. */
      bool overrun_error : 1;
      /** Framing error. */
      bool framing_error : 1;
      /** Noise detected. */
      bool noise_detected : 1;
    };
    /** All flags as a bit mask; non-zero if any error occurred. */
    uint8_t error_mask;
  };
} UARTRXErrorFlags;

/**
 * @brief Receive interrupt handler, called from the UART ISR for each received byte.
 *
 * @param dev Device.
 * @param data Received byte.
 * @param err_flags Errors detected with this byte.
 */
typedef void (*UARTRXInterruptHandler)(UARTDevice *dev, uint8_t data,
                                       const UARTRXErrorFlags *err_flags);
/**
 * @brief Transmit interrupt handler, called from the UART ISR when a byte can be sent.
 *
 * @param dev Device.
 */
typedef void (*UARTTXInterruptHandler)(UARTDevice *dev);

/**
 * @brief Initialize the device for receive and transmit.
 *
 * @param dev Device.
 */
void uart_init(UARTDevice *dev);

/**
 * @brief Initialize the device with open-drain instead of push-pull pins.
 *
 * Not supported on all SoCs.
 *
 * @param dev Device.
 */
void uart_init_open_drain(UARTDevice *dev);

/**
 * @brief Initialize the device for transmit only.
 *
 * @param dev Device.
 */
void uart_init_tx_only(UARTDevice *dev);

/**
 * @brief Initialize the device for receive only.
 *
 * @param dev Device.
 */
void uart_init_rx_only(UARTDevice *dev);

/**
 * @brief Deinitialize the device.
 *
 * @param dev Device.
 */
void uart_deinit(UARTDevice *dev);

/**
 * @brief Set the baud rate.
 *
 * @param dev Device.
 * @param baud_rate Baud rate in bits per second.
 */
void uart_set_baud_rate(UARTDevice *dev, uint32_t baud_rate);

/**
 * @brief Set the receive interrupt handler.
 *
 * @param dev Device.
 * @param irq_handler Handler, called from the ISR for each received byte.
 */
void uart_set_rx_interrupt_handler(UARTDevice *dev, UARTRXInterruptHandler irq_handler);

/**
 * @brief Set the transmit interrupt handler.
 *
 * @param dev Device.
 * @param irq_handler Handler, called from the ISR when a byte can be sent.
 */
void uart_set_tx_interrupt_handler(UARTDevice *dev, UARTTXInterruptHandler irq_handler);

/**
 * @brief Enable or disable the receive interrupt.
 *
 * @param dev Device.
 * @param enabled true to enable.
 */
void uart_set_rx_interrupt_enabled(UARTDevice *dev, bool enabled);
/**
 * @brief Enable or disable the transmit interrupt.
 *
 * @param dev Device.
 * @param enabled true to enable.
 */
void uart_set_tx_interrupt_enabled(UARTDevice *dev, bool enabled);

/**
 * @brief Write a byte.
 *
 * Blocks until the transmit buffer has room.
 *
 * @param dev Device.
 * @param data Byte to write.
 */
void uart_write_byte(UARTDevice *dev, uint8_t data);

/**
 * @brief Read a byte.
 *
 * Clears the framing and overrun error flags.
 *
 * @param dev Device.
 * @return Byte read.
 */
uint8_t uart_read_byte(UARTDevice *dev);

/**
 * @brief Start receiving into a circular DMA buffer.
 *
 * The device must have DMA configured. Received bytes are still delivered to the receive
 * handler. Some SoCs require @p buffer and @p length to be aligned to the data cache line.
 *
 * @param dev Device.
 * @param buffer DMA buffer.
 * @param length Size of @p buffer in bytes.
 */
void uart_start_rx_dma(UARTDevice *dev, void *buffer, uint32_t length);

/**
 * @brief Stop receiving through DMA.
 *
 * @param dev Device.
 */
void uart_stop_rx_dma(UARTDevice *dev);

/**
 * @brief Discard data pending in the receive DMA buffer.
 *
 * @param dev Device.
 */
void uart_clear_rx_dma_buffer(UARTDevice *dev);

/**
 * @brief Check whether a received byte is ready to be read.
 *
 * @param dev Device.
 * @return true if a byte is ready.
 */
bool uart_is_rx_ready(UARTDevice *dev);

/**
 * @brief Check for a receive overrun.
 *
 * Check before reading the received byte, as reading clears the flag.
 *
 * @param dev Device.
 * @return true on overrun.
 */
bool uart_has_rx_overrun(UARTDevice *dev);

/**
 * @brief Check for a framing error.
 *
 * Check before reading the received byte, as reading clears the flag.
 *
 * @param dev Device.
 * @return true on framing error.
 */
bool uart_has_rx_framing_error(UARTDevice *dev);

/**
 * @brief Check whether a byte can be written.
 *
 * @param dev Device.
 * @return true if the transmit buffer has room.
 */
bool uart_is_tx_ready(UARTDevice *dev);

/**
 * @brief Check whether the last byte has been fully sent.
 *
 * @param dev Device.
 * @return true if transmission is complete.
 */
bool uart_is_tx_complete(UARTDevice *dev);

/**
 * @brief Wait until the last byte has been fully sent.
 *
 * @param dev Device.
 */
void uart_wait_for_tx_complete(UARTDevice *dev);

/**
 * @brief Get the pending receive errors.
 *
 * @param dev Device.
 * @return Error flags; @c error_mask is non-zero if an error occurred.
 */
UARTRXErrorFlags uart_has_errored_out(UARTDevice *dev);

/**
 * @brief Clear all interrupt flags.
 *
 * @param dev Device.
 */
void uart_clear_all_interrupt_flags(UARTDevice *dev);


typedef struct UARTDeviceState {
  /** Initialized. */
  bool initialized;
  /** Receive handler. */
  UARTRXInterruptHandler rx_irq_handler;
  /** Transmit handler. */
  UARTTXInterruptHandler tx_irq_handler;
  /** Receive interrupt enabled. */
  bool rx_int_enabled;
  /** Transmit interrupt enabled. */
  bool tx_int_enabled;
  /** Circular RX DMA buffer, or NULL when RX is interrupt driven. */
  uint8_t *rx_dma_buffer;
  /** Size of rx_dma_buffer. */
  uint32_t rx_dma_length;
  /** Next byte of rx_dma_buffer to hand to the RX handler. */
  uint32_t rx_dma_index;
  /** The class port wrapping this one, if any. */
  const struct pbl_uart *uart;
} UARTDeviceState;

struct UARTDevice {
  /** Driver state. */
  UARTDeviceState *state;
  /** Peripheral. */
  USART_TypeDef *periph;
  /** RCC APB enable bit (RCC_APB1Periph_* or RCC_APB2Periph_*). */
  uint32_t rcc_apb_periph;
  /** On APB2 (USART1/6) rather than APB1. */
  bool apb2;
  /** TX pin, not connected if unused. */
  struct pbl_gpio tx;
  /** RX pin, not connected if unused. */
  struct pbl_gpio rx;
  /** CTS and RTS pins: hardware flow control when both are connected. */
  struct pbl_gpio cts;
  struct pbl_gpio rts;
  /** Alternate function of the pins (GPIO_AF_USARTx). */
  uint8_t af;
  /** Interrupt number. */
  int irqn;
  /** RX DMA stream, or NULL to keep RX interrupt driven. */
  const struct pbl_dma_stm32_stream *rx_dma;
};

/* UART interrupt handler, connected with PBL_IRQ_CONNECT() in the board file. */
void uart_irq_handler(UARTDevice *dev);

/** @brief An STM32 USART as a @ref drivers_uart port. */
struct pbl_uart_stm32 {
  /** Serial port. */
  struct pbl_uart uart;
  /** USART. */
  UARTDevice *dev;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_uart_ops pbl_uart_stm32_ops;
/** @endcond */

/**
 * @brief Define a @ref drivers_uart port on an STM32 USART.
 * @param sym Symbol of the port; the port is @c sym.uart.
 * @param _name Name.
 * @param _baudrate Initial baud rate.
 * @param _dev USART.
 */
#define PBL_UART_STM32_DEFINE(sym, _name, _baudrate, _dev)                       \
  PBL_UART_STATE_DEFINE(sym);                                                    \
  const struct pbl_uart_stm32 sym = {                                            \
    .uart = PBL_UART_INIT(sym, _name, &pbl_uart_stm32_ops, _baudrate, nullptr), \
    .dev = (_dev),                                                               \
  };                                                                             \
  PBL_DEVICE_REGISTER(sym, &sym.uart.dev)

/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F4 I2C (v1 controller), interrupt driven, as the stock firmware.

#include <errno.h>

#include <pbl/drivers/gpio/stm32.h>
#include <pbl/drivers/i2c/stm32.h>
#include <pbl/kernel/irq.h>
#include <pbl/soc/stm32/sleep.h>
#include <pbl/util/misc.h>

#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

#define I2C_NORMAL_MODE_CLOCK_SPEED_MAX 100000

#define I2C_READ_WRITE_BIT 0x01

//! Event expected on entry to each state.
static const uint32_t s_guard_events[] = {
  [PBL_I2C_STM32_START] = I2C_EVENT_MASTER_MODE_SELECT,
  [PBL_I2C_STM32_ADDRESS_TX] = I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED,
  [PBL_I2C_STM32_ADDRESS_RX] = I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED,
  [PBL_I2C_STM32_WRITE_DATA] = I2C_EVENT_MASTER_BYTE_TRANSMITTING,
  [PBL_I2C_STM32_END_WRITE] = I2C_EVENT_MASTER_BYTE_TRANSMITTED,
  [PBL_I2C_STM32_READ_DATA] = I2C_EVENT_MASTER_BYTE_RECEIVED,
};

static const struct pbl_i2c_stm32 *prv_i2c(const struct pbl_i2c_bus *bus) {
  return container_of(bus, const struct pbl_i2c_stm32, bus);
}

static uint32_t prv_pclk1(void) {
  RCC_ClocksTypeDef clocks;
  RCC_GetClocksFreq(&clocks);
  return clocks.PCLK1_Frequency;
}

//! Fast mode uses duty cycle 2 (divider 3); find a clock not above the target.
static uint32_t prv_clock_speed(uint32_t desired_hz) {
  const uint32_t divider = (desired_hz > I2C_NORMAL_MODE_CLOCK_SPEED_MAX) ? 3 : 2;
  const uint32_t pclk1 = prv_pclk1();
  uint32_t prescaler = pclk1 / (desired_hz * divider);
  if (pclk1 % (desired_hz * divider)) {
    prescaler++;
  }
  return pclk1 / (prescaler * divider);
}

static void prv_stop_block(const struct pbl_i2c_stm32 *i2c) {
  pbl_irq_lock();
  if (!i2c->state->stop_blocked) {
    i2c->state->stop_blocked = true;
    soc_stm32_stop_block();
  }
  pbl_irq_unlock();
}

static void prv_stop_release(const struct pbl_i2c_stm32 *i2c) {
  pbl_irq_lock();
  if (i2c->state->stop_blocked) {
    i2c->state->stop_blocked = false;
    soc_stm32_stop_release();
  }
  pbl_irq_unlock();
}

static void prv_disable_all_interrupts(I2C_TypeDef *i2c) {
  i2c->CR2 &= ~(I2C_CR2_ITEVTEN | I2C_CR2_ITERREN | I2C_CR2_ITBUFEN);
}

// The pins are only handed to the peripheral while the bus is enabled, as the
// stock firmware does: on a switched rail they sit low while it is off, and an
// I2C seeing that can latch BUSY so that no reset clears it.
static void prv_pins_input(const struct pbl_i2c_stm32 *i2c) {
  pbl_gpio_configure(&i2c->scl, PBL_GPIO_INPUT);
  pbl_gpio_configure(&i2c->sda, PBL_GPIO_INPUT);
}

static int prv_init(const struct pbl_i2c_bus *bus) {
  const struct pbl_i2c_stm32 *i2c = prv_i2c(bus);

  prv_pins_input(i2c);

  RCC_APB1PeriphClockCmd(i2c->rcc, ENABLE);
  I2C_DeInit(i2c->i2c);
  RCC_APB1PeriphClockCmd(i2c->rcc, DISABLE);

  pbl_irq_enable(i2c->ev_irqn);
  pbl_irq_enable(i2c->er_irqn);

  return 0;
}

static void prv_enable(const struct pbl_i2c_bus *bus) {
  const struct pbl_i2c_stm32 *i2c = prv_i2c(bus);

  pbl_gpio_stm32_af(&i2c->scl, i2c->scl_af, PBL_GPIO_OPEN_DRAIN, GPIO_Speed_50MHz);
  pbl_gpio_stm32_af(&i2c->sda, i2c->sda_af, PBL_GPIO_OPEN_DRAIN, GPIO_Speed_50MHz);

  RCC_APB1PeriphClockCmd(i2c->rcc, ENABLE);

  I2C_InitTypeDef init;
  I2C_StructInit(&init);
  if (i2c->clock_hz > I2C_NORMAL_MODE_CLOCK_SPEED_MAX) {
    init.I2C_DutyCycle = I2C_DutyCycle_2;
  }
  init.I2C_ClockSpeed = prv_clock_speed(i2c->clock_hz);
  init.I2C_Ack = I2C_Ack_Enable;
  I2C_Init(i2c->i2c, &init);
  I2C_Cmd(i2c->i2c, ENABLE);
}

static void prv_disable(const struct pbl_i2c_bus *bus) {
  const struct pbl_i2c_stm32 *i2c = prv_i2c(bus);

  I2C_DeInit(i2c->i2c);
  RCC_APB1PeriphClockCmd(i2c->rcc, DISABLE);
  prv_pins_input(i2c);
}

static bool prv_is_busy(const struct pbl_i2c_stm32 *i2c) {
  return (i2c->i2c->SR2 & I2C_SR2_BUSY) != 0;
}

static void prv_begin(const struct pbl_i2c_stm32 *i2c) {
  i2c->state->xfer_state = PBL_I2C_STM32_START;
  i2c->state->idx = 0;
  i2c->i2c->CR1 |= I2C_CR1_ACK;
  i2c->i2c->CR1 |= I2C_CR1_START;
}

static int prv_start(const struct pbl_i2c_bus *bus, uint16_t addr, struct pbl_i2c_msg *msgs,
                     uint8_t num_msgs) {
  const struct pbl_i2c_stm32 *i2c = prv_i2c(bus);
  struct pbl_i2c_stm32_state *state = i2c->state;

  for (uint8_t i = 0; i < num_msgs; i++) {
    if (msgs[i].len == 0U) {
      return -ENOTSUP;
    }
  }

  // A glitch can latch BUSY with the bus idle; only a reset clears it.
  if (prv_is_busy(i2c)) {
    i2c->i2c->CR1 |= I2C_CR1_SWRST;
    i2c->i2c->CR1 &= ~I2C_CR1_SWRST;
    prv_enable(bus);
    if (prv_is_busy(i2c)) {
      return -EBUSY;
    }
  }

  state->msgs = msgs;
  state->num_msgs = num_msgs;
  state->msg = 0;
  state->addr = addr;
  prv_stop_block(i2c);

  prv_begin(i2c);
  i2c->i2c->CR2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITERREN;

  return 0;
}

static void prv_abort(const struct pbl_i2c_bus *bus) {
  const struct pbl_i2c_stm32 *i2c = prv_i2c(bus);

  prv_disable_all_interrupts(i2c->i2c);
  i2c->i2c->CR1 |= I2C_CR1_STOP;
  i2c->state->xfer_state = PBL_I2C_STM32_COMPLETE;
  prv_stop_release(i2c);
}

static void prv_end_transfer_irq(const struct pbl_i2c_stm32 *i2c, int status) {
  prv_disable_all_interrupts(i2c->i2c);
  i2c->i2c->CR1 |= I2C_CR1_STOP;
  i2c->state->xfer_state = PBL_I2C_STM32_COMPLETE;
  prv_stop_release(i2c);
  pbl_i2c_bus_complete(&i2c->bus, status);
}

//! The current message is done: continue a write, restart for the next message or stop.
static void prv_next_msg(const struct pbl_i2c_stm32 *i2c) {
  struct pbl_i2c_stm32_state *state = i2c->state;

  state->msg++;
  state->idx = 0;
  if (state->msg == state->num_msgs) {
    prv_end_transfer_irq(i2c, 0);
  } else {
    prv_begin(i2c);
  }
}

void pbl_i2c_stm32_ev_irq_handler(const struct pbl_i2c_stm32 *i2c) {
  I2C_TypeDef *regs = i2c->i2c;
  struct pbl_i2c_stm32_state *state = i2c->state;

  if (state->xfer_state == PBL_I2C_STM32_COMPLETE) {
    // Spurious interrupt
    regs->CR2 &= ~(I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN);
    return;
  }

  // Spurious byte-transmitted and event-less interrupts follow repeated
  // starts; ignore anything that is not the expected event.
  if (I2C_CheckEvent(regs, s_guard_events[state->xfer_state]) == ERROR) {
    return;
  }

  struct pbl_i2c_msg *msg = &state->msgs[state->msg];
  const uint8_t addr = state->addr << 1;

  switch (state->xfer_state) {
    case PBL_I2C_STM32_START:
      if ((msg->flags & PBL_I2C_MSG_READ) != 0U) {
        regs->DR = addr | I2C_READ_WRITE_BIT;
        if (msg->len == 1) {
          // NACK the only byte to stop the device sending.
          regs->CR1 &= ~I2C_CR1_ACK;
        }
        state->xfer_state = PBL_I2C_STM32_ADDRESS_RX;
      } else {
        regs->DR = addr & ~I2C_READ_WRITE_BIT;
        state->xfer_state = PBL_I2C_STM32_ADDRESS_TX;
      }
      break;

    case PBL_I2C_STM32_ADDRESS_TX:
      regs->CR2 |= I2C_CR2_ITBUFEN;
      state->xfer_state = PBL_I2C_STM32_WRITE_DATA;
      regs->DR = msg->buf[state->idx++];
      break;

    case PBL_I2C_STM32_WRITE_DATA:
      if (state->idx == msg->len) {
        if (state->msg + 1 < state->num_msgs &&
            (state->msgs[state->msg + 1].flags & PBL_I2C_MSG_NOSTART) != 0U) {
          state->msg++;
          state->idx = 0;
          msg++;
        } else {
          regs->CR2 &= ~I2C_CR2_ITBUFEN;
          state->xfer_state = PBL_I2C_STM32_END_WRITE;
          break;
        }
      }
      regs->DR = msg->buf[state->idx++];
      break;

    case PBL_I2C_STM32_END_WRITE:
      prv_next_msg(i2c);
      break;

    case PBL_I2C_STM32_ADDRESS_RX:
      regs->CR2 |= I2C_CR2_ITBUFEN;
      state->xfer_state = PBL_I2C_STM32_READ_DATA;
      break;

    case PBL_I2C_STM32_READ_DATA:
      msg->buf[state->idx++] = regs->DR;
      if (state->idx + 1 == msg->len) {
        // NACK the last byte.
        regs->CR1 &= ~I2C_CR1_ACK;
      } else if (state->idx == msg->len) {
        regs->CR2 &= ~I2C_CR2_ITBUFEN;
        prv_next_msg(i2c);
      }
      break;

    default:
      PBL_ASSERTN(false);
      break;
  }
}

void pbl_i2c_stm32_er_irq_handler(const struct pbl_i2c_stm32 *i2c) {
  I2C_TypeDef *regs = i2c->i2c;
  const enum pbl_i2c_stm32_xfer_state xfer_state = i2c->state->xfer_state;

  if (xfer_state == PBL_I2C_STM32_COMPLETE) {
    regs->CR2 &= ~I2C_CR2_ITERREN;
    return;
  }

  // A NACK of the address: no device there.
  const bool no_device =
      (regs->SR1 & I2C_SR1_AF) != 0U &&
      (xfer_state == PBL_I2C_STM32_ADDRESS_TX || xfer_state == PBL_I2C_STM32_ADDRESS_RX);

  // Overrun, bus error or NACK: end the transfer.
  regs->SR1 &= ~(I2C_SR1_OVR | I2C_SR1_BERR | I2C_SR1_AF | I2C_SR1_ARLO);
  prv_end_transfer_irq(i2c, no_device ? -ENXIO : -EIO);
}

const struct pbl_i2c_bus_ops pbl_i2c_stm32_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .start = prv_start,
  .abort = prv_abort,
};

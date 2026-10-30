/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/kernel/irq.h>

#include <stm32f4xx.h>

enum pbl_i2c_stm32_xfer_state {
  PBL_I2C_STM32_START,
  PBL_I2C_STM32_ADDRESS_TX,
  PBL_I2C_STM32_ADDRESS_RX,
  PBL_I2C_STM32_WRITE_DATA,
  PBL_I2C_STM32_END_WRITE,
  PBL_I2C_STM32_READ_DATA,
  PBL_I2C_STM32_COMPLETE,
};

struct pbl_i2c_stm32_state {
  enum pbl_i2c_stm32_xfer_state xfer_state;
  struct pbl_i2c_msg *msgs;
  uint8_t num_msgs;
  uint8_t msg;
  uint16_t addr;
  uint32_t idx;
  //! A transfer holds off STOP mode, which would stop the controller.
  bool stop_blocked;
};

struct pbl_i2c_stm32 {
  /** I2C bus. */
  struct pbl_i2c_bus bus;
  /** Driver runtime state. */
  struct pbl_i2c_stm32_state *state;
  /** Controller. */
  I2C_TypeDef *i2c;
  /** RCC APB1 enable bit. */
  uint32_t rcc;
  /** Clock speed in Hz. */
  uint32_t clock_hz;
  /** SCL pin and its alternate function. */
  struct pbl_gpio scl;
  uint8_t scl_af;
  /** SDA pin and its alternate function. */
  struct pbl_gpio sda;
  uint8_t sda_af;
  /** Event and error interrupts. */
  pbl_irq_t ev_irqn;
  pbl_irq_t er_irqn;
};

extern const struct pbl_i2c_bus_ops pbl_i2c_stm32_ops;

#define PBL_I2C_STM32_DEFINE(sym, _name, _inst, _rcc, _clock_hz, _scl, _scl_af, _sda, _sda_af, \
                             _deps)                                                            \
  PBL_I2C_BUS_STATE_DEFINE(sym);                                                               \
  static struct pbl_i2c_stm32_state sym##_stm32_state;                                         \
  const struct pbl_i2c_stm32 sym = {                                                           \
    .bus = PBL_I2C_BUS_INIT(sym, _name, &pbl_i2c_stm32_ops, _deps),                            \
    .state = &sym##_stm32_state,                                                               \
    .i2c = _inst,                                                                              \
    .rcc = (_rcc),                                                                             \
    .clock_hz = (_clock_hz),                                                                   \
    .scl = _scl,                                                                               \
    .scl_af = (_scl_af),                                                                       \
    .sda = _sda,                                                                               \
    .sda_af = (_sda_af),                                                                       \
    .ev_irqn = PBL_IRQN(_inst##_EV),                                                           \
    .er_irqn = PBL_IRQN(_inst##_ER),                                                           \
  };                                                                                           \
  PBL_DEVICE_REGISTER(sym, &sym.bus.dev)

void pbl_i2c_stm32_ev_irq_handler(const struct pbl_i2c_stm32 *i2c);
void pbl_i2c_stm32_er_irq_handler(const struct pbl_i2c_stm32 *i2c);

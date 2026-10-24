/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F4 EXTI for GPIO pins: line N serves pin N of the port routed to it
//! through SYSCFG.

#include <pbl/drivers/exti.h>
#include <pbl/kernel/irq.h>

#include <stm32f4xx.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

#define NUM_GPIO_LINES 16

#define EXTI_IRQ_PRIO 14

static ExtiHandlerCallback s_callbacks[NUM_GPIO_LINES];

static pbl_irq_t prv_irqn(uint8_t line) {
  if (line <= 4) {
    return PBL_IRQN(EXTI0) + line;
  }
  if (line <= 9) {
    return PBL_IRQN(EXTI9_5);
  }
  return PBL_IRQN(EXTI15_10);
}

static void prv_dispatch(uint8_t first, uint8_t last) {
  for (uint8_t line = first; line <= last; line++) {
    const uint32_t mask = 1U << line;
    if (EXTI->PR & mask) {
      EXTI->PR = mask;
      if (s_callbacks[line]) {
        s_callbacks[line]();
      }
    }
  }
}

static void prv_exti0(void) {
  prv_dispatch(0, 0);
}
static void prv_exti1(void) {
  prv_dispatch(1, 1);
}
static void prv_exti2(void) {
  prv_dispatch(2, 2);
}
static void prv_exti3(void) {
  prv_dispatch(3, 3);
}
static void prv_exti4(void) {
  prv_dispatch(4, 4);
}
static void prv_exti9_5(void) {
  prv_dispatch(5, 9);
}
static void prv_exti15_10(void) {
  prv_dispatch(10, 15);
}

PBL_IRQ_CONNECT(EXTI0, EXTI_IRQ_PRIO, prv_exti0, , 0);
PBL_IRQ_CONNECT(EXTI1, EXTI_IRQ_PRIO, prv_exti1, , 0);
PBL_IRQ_CONNECT(EXTI2, EXTI_IRQ_PRIO, prv_exti2, , 0);
PBL_IRQ_CONNECT(EXTI3, EXTI_IRQ_PRIO, prv_exti3, , 0);
PBL_IRQ_CONNECT(EXTI4, EXTI_IRQ_PRIO, prv_exti4, , 0);
PBL_IRQ_CONNECT(EXTI9_5, EXTI_IRQ_PRIO, prv_exti9_5, , 0);
PBL_IRQ_CONNECT(EXTI15_10, EXTI_IRQ_PRIO, prv_exti15_10, , 0);

void exti_configure_pin(ExtiConfig cfg, ExtiTrigger trigger, ExtiHandlerCallback cb) {
  PBL_ASSERTN(cfg.exti_line < NUM_GPIO_LINES);

  const uint32_t mask = 1U << cfg.exti_line;

  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
  SYSCFG_EXTILineConfig(cfg.exti_port_source, cfg.exti_line);

  s_callbacks[cfg.exti_line] = cb;

  if (trigger == ExtiTrigger_Rising || trigger == ExtiTrigger_RisingFalling) {
    EXTI->RTSR |= mask;
  } else {
    EXTI->RTSR &= ~mask;
  }
  if (trigger == ExtiTrigger_Falling || trigger == ExtiTrigger_RisingFalling) {
    EXTI->FTSR |= mask;
  } else {
    EXTI->FTSR &= ~mask;
  }
}

void exti_enable(ExtiConfig config) {
  const uint32_t mask = 1U << config.exti_line;

  EXTI->PR = mask;
  EXTI->IMR |= mask;
  pbl_irq_enable(prv_irqn(config.exti_line));
}

void exti_disable(ExtiConfig config) {
  EXTI->IMR &= ~(1U << config.exti_line);
  // Shared vectors stay enabled; their lines are masked individually.
  if (config.exti_line <= 4) {
    pbl_irq_disable(prv_irqn(config.exti_line));
  }
}

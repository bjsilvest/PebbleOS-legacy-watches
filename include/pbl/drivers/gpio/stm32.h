/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/drivers/gpio.h>
#include <pbl/util/misc.h>

#include <stm32f4xx.h>

/**
 * @defgroup drivers_gpio_stm32 STM32 GPIO
 * @ingroup drivers_gpio
 * @brief @ref drivers_gpio ports of the STM32F4 SoCs.
 * @{
 */

/** @brief STM32 GPIO port. */
struct pbl_gpio_stm32 {
  /** GPIO port. */
  struct pbl_gpio_port port;
  /** Port registers. */
  GPIO_TypeDef *regs;
  /** Port index (A = 0), also its RCC AHB1ENR bit. */
  uint8_t index;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_gpio_stm32 pbl_gpio_stm32_a;
extern const struct pbl_gpio_stm32 pbl_gpio_stm32_b;
extern const struct pbl_gpio_stm32 pbl_gpio_stm32_c;
extern const struct pbl_gpio_stm32 pbl_gpio_stm32_d;
extern const struct pbl_gpio_stm32 pbl_gpio_stm32_e;
extern const struct pbl_gpio_stm32 pbl_gpio_stm32_f;
extern const struct pbl_gpio_stm32 pbl_gpio_stm32_g;
extern const struct pbl_gpio_stm32 pbl_gpio_stm32_h;
/** @endcond */

#define STM32_GPIOA (&pbl_gpio_stm32_a.port)
#define STM32_GPIOB (&pbl_gpio_stm32_b.port)
#define STM32_GPIOC (&pbl_gpio_stm32_c.port)
#define STM32_GPIOD (&pbl_gpio_stm32_d.port)
#define STM32_GPIOE (&pbl_gpio_stm32_e.port)
#define STM32_GPIOF (&pbl_gpio_stm32_f.port)
#define STM32_GPIOG (&pbl_gpio_stm32_g.port)
#define STM32_GPIOH (&pbl_gpio_stm32_h.port)

/** @brief Registers of the port a pin is on. */
static inline GPIO_TypeDef *pbl_gpio_stm32_regs(const struct pbl_gpio *gpio) {
  return container_of(gpio->port, const struct pbl_gpio_stm32, port)->regs;
}

/**
 * @brief Hand a pin to a peripheral.
 *
 * @param gpio Pin on an STM32 port.
 * @param af Alternate function number (GPIO_AF_*).
 * @param flags @c PBL_GPIO_PULL_UP, @c PBL_GPIO_PULL_DOWN and/or
 *        @c PBL_GPIO_OPEN_DRAIN.
 * @param speed Output speed (GPIO_*_Speed).
 */
void pbl_gpio_stm32_af(const struct pbl_gpio *gpio, uint8_t af, uint32_t flags,
                       GPIOSpeed_TypeDef speed);

/**
 * @brief Put a pin in analog mode (lowest power, or for the ADC).
 *
 * @param gpio Pin on an STM32 port.
 */
void pbl_gpio_stm32_analog(const struct pbl_gpio *gpio);

/** @} */

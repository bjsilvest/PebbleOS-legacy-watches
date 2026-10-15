/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/drivers/gpio/stm32.h>

static const struct pbl_gpio_stm32 *prv_port(const struct pbl_gpio_port *port) {
  return container_of(port, const struct pbl_gpio_stm32, port);
}

static GPIOPuPd_TypeDef prv_pull(uint32_t flags) {
  if (flags & PBL_GPIO_PULL_UP) {
    return GPIO_PuPd_UP;
  }

  if (flags & PBL_GPIO_PULL_DOWN) {
    return GPIO_PuPd_DOWN;
  }

  return GPIO_PuPd_NOPULL;
}

static void prv_init_pin(GPIO_TypeDef *regs, uint8_t pin, GPIOMode_TypeDef mode, uint32_t flags,
                         GPIOSpeed_TypeDef speed) {
  GPIO_InitTypeDef init = {
    .GPIO_Pin = 1U << pin,
    .GPIO_Mode = mode,
    .GPIO_Speed = speed,
    .GPIO_OType = (flags & PBL_GPIO_OPEN_DRAIN) ? GPIO_OType_OD : GPIO_OType_PP,
    .GPIO_PuPd = prv_pull(flags),
  };

  GPIO_Init(regs, &init);
}

static int prv_configure(const struct pbl_gpio_port *port, uint8_t pin, uint32_t flags) {
  GPIO_TypeDef *regs = prv_port(port)->regs;

  if (flags & PBL_GPIO_OUTPUT) {
    if (flags & PBL_GPIO_OUTPUT_INIT_HIGH) {
      GPIO_SetBits(regs, 1U << pin);
    } else if (flags & PBL_GPIO_OUTPUT_INIT_LOW) {
      GPIO_ResetBits(regs, 1U << pin);
    }

    prv_init_pin(regs, pin, GPIO_Mode_OUT, flags, GPIO_Low_Speed);
  } else if (flags & PBL_GPIO_INPUT) {
    prv_init_pin(regs, pin, GPIO_Mode_IN, flags, GPIO_Low_Speed);
  } else {
    return -EINVAL;
  }

  return 0;
}

static int prv_get(const struct pbl_gpio_port *port, uint8_t pin) {
  return GPIO_ReadInputDataBit(prv_port(port)->regs, 1U << pin) != Bit_RESET;
}

static int prv_set(const struct pbl_gpio_port *port, uint8_t pin, bool level) {
  GPIO_WriteBit(prv_port(port)->regs, 1U << pin, level ? Bit_SET : Bit_RESET);

  return 0;
}

void pbl_gpio_stm32_af(const struct pbl_gpio *gpio, uint8_t af, uint32_t flags,
                       GPIOSpeed_TypeDef speed) {
  GPIO_TypeDef *regs = pbl_gpio_stm32_regs(gpio);

  GPIO_PinAFConfig(regs, gpio->pin, af);
  prv_init_pin(regs, gpio->pin, GPIO_Mode_AF, flags | gpio->flags, speed);
}

void pbl_gpio_stm32_analog(const struct pbl_gpio *gpio) {
  prv_init_pin(pbl_gpio_stm32_regs(gpio), gpio->pin, GPIO_Mode_AN, 0, GPIO_Low_Speed);
}

static int prv_init(const struct pbl_device *dev) {
  const struct pbl_gpio_stm32 *port = container_of(dev, const struct pbl_gpio_stm32, port.dev);

  RCC->AHB1ENR |= 1U << port->index;
  (void)RCC->AHB1ENR;

  return 0;
}

static const struct pbl_gpio_port_ops s_ops = {
  .configure = prv_configure,
  .get = prv_get,
  .set = prv_set,
};

#define GPIO_STM32_DEFINE(sym, _name, _regs, _index)                                         \
  PBL_DEVICE_STATE_DEFINE(sym);                                                              \
  const struct pbl_gpio_stm32 sym = {                                                        \
    .port = {.dev = PBL_DEVICE_INIT(sym, _name, prv_init, nullptr, nullptr), .ops = &s_ops}, \
    .regs = (_regs),                                                                         \
    .index = (_index),                                                                       \
  };                                                                                         \
  PBL_DEVICE_REGISTER(sym, &sym.port.dev)

GPIO_STM32_DEFINE(pbl_gpio_stm32_a, "gpio_a", GPIOA, 0);
GPIO_STM32_DEFINE(pbl_gpio_stm32_b, "gpio_b", GPIOB, 1);
GPIO_STM32_DEFINE(pbl_gpio_stm32_c, "gpio_c", GPIOC, 2);
GPIO_STM32_DEFINE(pbl_gpio_stm32_d, "gpio_d", GPIOD, 3);
#ifdef CONFIG_HAL_STM32_STM32F439
GPIO_STM32_DEFINE(pbl_gpio_stm32_e, "gpio_e", GPIOE, 4);
GPIO_STM32_DEFINE(pbl_gpio_stm32_f, "gpio_f", GPIOF, 5);
GPIO_STM32_DEFINE(pbl_gpio_stm32_g, "gpio_g", GPIOG, 6);
#endif
GPIO_STM32_DEFINE(pbl_gpio_stm32_h, "gpio_h", GPIOH, 7);

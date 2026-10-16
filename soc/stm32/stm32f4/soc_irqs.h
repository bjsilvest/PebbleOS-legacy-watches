/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#if defined(CONFIG_HAL_STM32_STM32F205)
#include "soc_irqs_stm32f205.h"
#elif defined(CONFIG_HAL_STM32_STM32F412)
#include "soc_irqs_stm32f412.h"
#elif defined(CONFIG_HAL_STM32_STM32F439)
#include "soc_irqs_stm32f439.h"
#else
#error "Unknown STM32F4 variant"
#endif

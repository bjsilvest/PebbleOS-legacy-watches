/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/temperature.h>

/**
 * @defgroup drivers_temp_stm32 STM32
 * @ingroup drivers_temp
 * @brief STM32 internal @ref drivers_temp sensor.
 * @{
 */

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_temp pbl_temp_stm32;
/** @endcond */

/** @brief The die temperature sensor. */
#define STM32_TEMP (&pbl_temp_stm32)

/** @} */

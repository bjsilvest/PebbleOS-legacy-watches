/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

/**
 * @brief Keep the SoC out of STOP mode, e.g. while a peripheral needs its
 * clock. Calls nest; each block needs a matching release.
 */
void soc_stm32_stop_block(void);

/** @brief Undo soc_stm32_stop_block(). */
void soc_stm32_stop_release(void);

/** @brief Whether nothing currently blocks STOP mode. */
bool soc_stm32_stop_is_allowed(void);

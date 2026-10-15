/* SPDX-FileCopyrightText: 2025 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/logging/logging.h>

#include <system/passert.h>

void __assert_func(const char *file, int line, const char *func, const char *e) {
  // Record the caller's LR, not this function's, so the reboot reason points
  // at the failed assert().
  const uint32_t lr = (uint32_t)(uintptr_t)PBL_RETURN_ADDRESS(0);

  PBL_LOG_ERR("assert in %s:%d", file, line);
  PBL_LOG_ERR("%s, expr: %s", func, e);
  PBL_ASSERTN_LR(0, lr);
}

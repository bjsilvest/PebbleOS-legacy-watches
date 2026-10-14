/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/drivers/mcu.h>

// RM0402, section 31.2 (unique device ID register)
#define UID_BASE_ADDR (0x1FFF7A10)

StatusCode mcu_get_serial(void *buf, size_t *buf_sz) {
  const uint32_t *uid = (const uint32_t *)UID_BASE_ADDR;
  const uint32_t id[3] = {uid[0], uid[1], uid[2]};

  if (*buf_sz < sizeof(id)) {
    return E_OUT_OF_MEMORY;
  }

  memcpy(buf, id, sizeof(id));
  *buf_sz = sizeof(id);

  return S_SUCCESS;
}

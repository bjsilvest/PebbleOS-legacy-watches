/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F4 OTP area. Read-only: the slots were written at the factory and
//! writing OTP cannot be undone, so writes are always refused.

#include <pbl/drivers/otp.h>

#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

// RM0402, section 3.5 (OTP area)
#define OTP_SLOTS_BASE_ADDR (0x1FFF7800)
#define OTP_LOCKS_BASE_ADDR (0x1FFF7A00)
#define OTP_SLOT_SIZE_BYTES (32)

char *otp_get_slot(const uint8_t index) {
  PBL_ASSERTN(index < NUM_OTP_SLOTS);
  return (char *)(OTP_SLOTS_BASE_ADDR + (OTP_SLOT_SIZE_BYTES * index));
}

uint8_t *otp_get_lock(const uint8_t index) {
  PBL_ASSERTN(index < NUM_OTP_SLOTS);
  return (uint8_t *)(OTP_LOCKS_BASE_ADDR + index);
}

bool otp_is_locked(const uint8_t index) {
  return (*otp_get_lock(index) == 0);
}

OtpWriteResult otp_write_slot(const uint8_t index, const char *value) {
  return otp_is_locked(index) ? OtpWriteFailAlreadyWritten : OtpWriteFailCorrupt;
}

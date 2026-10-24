/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/kernel/syscall.h>
#include <pbl/services/alarms/alarm.h>

#ifdef CONFIG_SERVICE_HRM
#include <pbl/drivers/hrm.h>
#endif

PBL_SYSCALL(bool, sys_alarm_get_next_enabled, (OUT, time_t *, timestamp_out)) {
  return alarm_get_next_enabled_alarm(timestamp_out);
}

PBL_SYSCALL(bool, sys_hrm_manager_is_hrm_present) {
#ifdef CONFIG_SERVICE_HRM
  return hrm_is_present(HRM);
#else
  return false;
#endif
}

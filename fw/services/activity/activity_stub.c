/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Activity and workout API without CONFIG_SERVICE_ACTIVITY: no tracking, no data. The health
// API then reports everything as unavailable, as on watches that never had Health.

#include <string.h>

#include <pbl/services/activity/activity.h>
#include <pbl/services/activity/activity_algorithm.h>
#include <pbl/services/activity/insights_settings.h>
#include <pbl/services/activity/workout_service.h>

#include <syscall/syscall_internal.h>

bool activity_init(void) {
  return false;
}

bool activity_is_initialized(void) {
  return false;
}

DEFINE_SYSCALL(bool, sys_activity_is_initialized, void) {
  return false;
}

bool activity_start_tracking(bool test_mode) {
  return false;
}

bool activity_stop_tracking(void) {
  return false;
}

bool activity_tracking_on(void) {
  return false;
}

void activity_set_enabled(bool enable) {
}

bool activity_is_hrm_present(void) {
  return false;
}

bool activity_get_metric(ActivityMetric metric, uint32_t history_len, int32_t *history) {
  if (history) {
    memset(history, 0, history_len * sizeof(*history));
  }
  return false;
}

DEFINE_SYSCALL(bool, sys_activity_get_metric, ActivityMetric metric, uint32_t history_len,
               int32_t *history) {
  return false;
}

DEFINE_SYSCALL(bool, sys_activity_get_sessions, uint32_t *session_entries,
               ActivitySession *sessions) {
  return false;
}

DEFINE_SYSCALL(bool, sys_activity_prefs_heart_rate_is_enabled, void) {
  return false;
}

DEFINE_SYSCALL(bool, sys_activity_get_minute_history, HealthMinuteData *minute_data,
               uint32_t *num_records, time_t *utc_start) {
  return false;
}

DEFINE_SYSCALL(bool, sys_activity_get_step_averages, enum pbl_weekday day_of_week,
               ActivityMetricAverages *averages) {
  return false;
}

DEFINE_SYSCALL(bool, sys_activity_sessions_is_session_type_ongoing, ActivitySessionType type) {
  return false;
}

void activity_algorithm_enable_activity_tracking(bool enable) {
}

void activity_algorithm_send_minutes(void) {
}

uint16_t activity_insights_settings_get_version(void) {
  return 0;
}

void workout_service_init(void) {
}

void workout_service_health_event_handler(PebbleHealthEvent *event) {
}

void workout_service_activity_event_handler(PebbleActivityEvent *event) {
}

void workout_service_workout_event_handler(PebbleWorkoutEvent *event) {
}

bool workout_service_is_workout_ongoing(void) {
  return false;
}

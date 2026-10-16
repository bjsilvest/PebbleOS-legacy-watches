/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Health API without activity tracking: nothing is supported and no data is returned.

#include "health_service.h"
#include "health_service_private.h"

#include <shell/prefs_syscalls.h>

HealthServiceAccessibilityMask health_service_metric_accessible(HealthMetric metric,
                                                                time_t time_start,
                                                                time_t time_end) {
  return HealthServiceAccessibilityMaskNotSupported;
}

HealthServiceAccessibilityMask health_service_metric_averaged_accessible(
    HealthMetric metric, time_t time_start, time_t time_end, HealthServiceTimeScope scope) {
  return HealthServiceAccessibilityMaskNotSupported;
}

HealthServiceAccessibilityMask health_service_metric_aggregate_averaged_accessible(
    HealthMetric metric, time_t time_start, time_t time_end, HealthAggregation aggregation,
    HealthServiceTimeScope scope) {
  return HealthServiceAccessibilityMaskNotSupported;
}

HealthServiceAccessibilityMask health_service_any_activity_accessible(
    HealthActivityMask activity_mask, time_t start_time, time_t end_time) {
  return HealthServiceAccessibilityMaskNotSupported;
}

HealthValue health_service_sum_today(HealthMetric metric) {
  return 0;
}

HealthValue health_service_sum(HealthMetric metric, time_t time_start, time_t time_end) {
  return 0;
}

HealthValue health_service_sum_averaged(HealthMetric metric, time_t time_start, time_t time_end,
                                        HealthServiceTimeScope scope) {
  return 0;
}

HealthValue health_service_peek_current_value(HealthMetric metric) {
  return 0;
}

HealthValue health_service_aggregate_averaged(HealthMetric metric, time_t time_start,
                                              time_t time_end, HealthAggregation aggregation,
                                              HealthServiceTimeScope scope) {
  return 0;
}

uint16_t health_service_peek_hrv_ppi_ms(void) {
  return 0;
}

bool health_service_events_subscribe(HealthEventHandler handler, void *context) {
  return false;
}

bool health_service_events_unsubscribe(void) {
  return false;
}

HealthMetricAlert *health_service_register_metric_alert(HealthMetric metric,
                                                        HealthValue threshold) {
  return nullptr;
}

bool health_service_cancel_metric_alert(HealthMetricAlert *alert) {
  return false;
}

bool health_service_set_hrv_sample_period(uint16_t interval_sec) {
  return false;
}

bool health_service_set_heart_rate_sample_period(uint16_t interval_sec) {
  return false;
}

uint16_t health_service_get_heart_rate_sample_period_expiration_sec(void) {
  return 0;
}

uint32_t health_service_get_minute_history(HealthMinuteData *minute_data, uint32_t max_records,
                                           time_t *time_start, time_t *time_end) {
  return 0;
}

HealthActivityMask health_service_peek_current_activities(void) {
  return HealthActivityNone;
}

void health_service_activities_iterate(HealthActivityMask activity_mask, time_t time_start,
                                       time_t time_end, HealthIterationDirection direction,
                                       HealthActivityIteratorCB callback, void *context) {
}

// Apps also use this for their own distances, so it keeps following the setting
MeasurementSystem health_service_get_measurement_system_for_display(HealthMetric metric) {
  if (metric != HealthMetricWalkedDistanceMeters) {
    return MeasurementSystemUnknown;
  }
  switch (sys_shell_prefs_get_units_distance()) {
    case UnitsDistance_Miles:
      return MeasurementSystemImperial;
    case UnitsDistance_KM:
      return MeasurementSystemMetric;
    default:
      return MeasurementSystemUnknown;
  }
}

void health_service_state_init(HealthServiceState *state) {
  *state = (HealthServiceState){};
}

void health_service_state_deinit(HealthServiceState *state) {
}

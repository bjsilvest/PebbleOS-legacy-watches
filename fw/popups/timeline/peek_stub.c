/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Timeline peek (Quick View) without CONFIG_TIMELINE_PEEK: nothing is ever shown, so the
// watchface is never obstructed and the timeline always opens with the plain slide.

#include "peek.h"

#include <pbl/services/timeline/peek.h>

#include <applib/graphics/graphics.h>
#include <process_management/app_manager.h>

void timeline_peek_init(void) {
}

void timeline_peek_set_enabled(bool enabled) {
}

void timeline_peek_set_show_before_time(unsigned int before_time_s) {
}

void timeline_peek_handle_peek_event(PebbleTimelinePeekEvent *event) {
}

void timeline_peek_handle_process_start(void) {
}

void timeline_peek_handle_process_kill(void) {
}

void timeline_peek_dismiss(void) {
}

bool timeline_peek_is_first_event(void) {
  return false;
}

bool timeline_peek_is_future_empty(void) {
  return false;
}

void timeline_peek_get_item_id(TimelineItemId *item_id_out) {
  *item_id_out = UUID_INVALID;
}

int16_t timeline_peek_get_obstruction_origin_y(void) {
  GSize app_framebuffer_size;
  app_manager_get_framebuffer_size(&app_framebuffer_size);
  return app_framebuffer_size.h;
}

unsigned int timeline_peek_get_concurrent_height(unsigned int num_concurrent) {
  return 0;
}

// Only reached through the peek compositor transition, which nothing starts
void timeline_peek_draw_background(GContext *ctx, const GRect *frame, unsigned int num_concurrent) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, frame);
}

const TimelineEventImpl *timeline_peek_get_event_service(void) {
  return nullptr;
}

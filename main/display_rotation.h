#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Owns the panel's top-level display cycle: clock (kaleidoscope/idle,
// no takeover) -> each currently-printing printer, in turn -> weather
// (if enabled) -> message (if enabled) -> clock ..., spending each
// slot's own independent dwell time (clock_secs/printer_secs/
// weather_secs/message_secs, see main/settings.h) and skipping any that
// don't currently apply. A message with message_static set overrides
// this whole cycle instead of taking a turn in it - see main/settings.h.
// Starts a single background task that drives panel_takeover.h begin/
// end around the printer/weather/message slots; the clock slot is just
// "no takeover active", so it needs no takeover call of its own.
esp_err_t kaleidobox_display_rotation_init(void);

// Re-renders the message immediately if it's currently showing in
// static (exclusive) mode - so a settings save takes effect right
// away instead of waiting for the next time the message slot is
// (re-)entered, which for static mode might otherwise be "never,
// until static is toggled off and on again". No-op if the message
// isn't the active static display right now.
void kaleidobox_display_rotation_message_changed(void);

// Clear/Stop's "stop everything, including the rotation, so I can draw
// uninterrupted" - stronger than the existing draw-activity guard (a
// brief grace window), holds indefinitely until _resume() is called.
// Tears down whatever's currently showing immediately (does not wait
// for its own dwell timer), without restoring/resuming anything -
// callers are responsible for their own explicit stop (e.g.
// kaleidoscope) beforehand. Session-scoped only, not NVS-backed - a
// reboot mid-pause comes back rotating normally.
void kaleidobox_display_rotation_pause(void);
void kaleidobox_display_rotation_resume(void);
bool kaleidobox_display_rotation_is_paused(void);

#pragma once

#include "esp_err.h"

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

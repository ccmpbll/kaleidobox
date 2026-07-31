#pragma once

#include "esp_err.h"

// Owns the panel's top-level display cycle: clock (kaleidoscope/idle,
// no takeover) -> each currently-printing printer, in turn -> weather
// (if enabled) -> clock ..., spending each slot's own independent dwell
// time (clock_secs/printer_secs/weather_secs, see main/settings.h) and
// skipping any that don't currently apply. Starts a single background
// task that drives panel_takeover.h begin/end around the printer/
// weather slots; the clock slot is just "no takeover active", so it
// needs no takeover call of its own.
esp_err_t kaleidobox_display_rotation_init(void);

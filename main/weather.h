#pragma once

#include <stdbool.h>

// Weather takeover - while enabled (see main/settings.h's weather_*
// settings), main/display_rotation.c calls kaleidobox_weather_fetch()
// below once per weather rotation slot to fetch a fresh OpenWeatherMap
// reading, then kaleidobox_weather_render_last() to draw it once it's
// decided to actually take the panel. Not polled in the background -
// fetched exactly when a slot needs it, so the data shown is never
// more than a few seconds stale.

// Bitmask for weather_fields - which lines to render, independently
// selectable. Default (see settings.c) is TEMP|CONDITION|LOCATION.
#define WEATHER_FIELD_TEMP 0x0001
#define WEATHER_FIELD_CONDITION 0x0002
#define WEATHER_FIELD_HUMIDITY 0x0004
#define WEATHER_FIELD_WIND 0x0008
#define WEATHER_FIELD_FEELS_LIKE 0x0010
#define WEATHER_FIELD_HIGH_LOW 0x0020
#define WEATHER_FIELD_LOCATION 0x0040

// Fetches current weather into an internal buffer. Returns false (and
// leaves the matrix untouched) if no key/location is configured, or
// the fetch fails - the caller (display_rotation.c) checks this
// *before* taking over the panel, so a failed fetch never
// stops/restarts kaleidoscope for nothing. Does not itself check
// weather_enabled - the caller only offers a weather slot at all when
// enabled.
bool kaleidobox_weather_fetch(void);

// Renders the data from the most recent successful
// kaleidobox_weather_fetch() call directly to the matrix.
void kaleidobox_weather_render_last(void);

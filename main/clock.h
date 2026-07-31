#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t kaleidobox_clock_init(void);

// Applies the stored timezone (POSIX TZ format, see settings.h) to the
// C library's local-time machinery - setenv("TZ", ...) + tzset(). Empty
// (the default) means UTC. Called at boot and after any settings
// change, so localtime() reflects the new zone immediately rather than
// only after a reboot.
void kaleidobox_clock_apply_tz(void);

// (Re)starts SNTP time sync against the stored NTP server (see
// settings.h). Safe to call repeatedly - stops any already-running sync
// first, so both the first call at boot and a later call after the
// server setting changes behave the same way.
void kaleidobox_clock_start_sntp(void);

// Composites the current time onto a CANVAS_WIDTH*CANVAS_HEIGHT*3
// RGB888 frame buffer, if the clock is enabled (see
// kaleidobox_nvs_get_clock_mode() in settings.h) - a no-op otherwise.
// Called once per kaleidoscope frame, right before it's pushed to the
// matrix - see main/kaleidoscope.c.
void kaleidobox_clock_overlay(uint8_t *frame);

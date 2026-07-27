#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t kaleidobox_nvs_init(void);

// Kaleidoscope mode settings - see main/kaleidoscope.h for what each
// controls. fold_count is the mirrored-wedge repeat count around the
// center (e.g. 6/8/12); motion_zoom toggles rotation-only vs
// rotation+zoom.
uint8_t kaleidobox_nvs_get_fold_count(void);
esp_err_t kaleidobox_nvs_set_fold_count(uint8_t count);

bool kaleidobox_nvs_get_motion_zoom(void);
esp_err_t kaleidobox_nvs_set_motion_zoom(bool enable);

// Draw-mode push behavior: instant (each pixel edit sent live over the
// /ws/draw WebSocket) vs draw-then-submit (client batches edits, one
// full-grid POST when done).
bool kaleidobox_nvs_get_instant_draw(void);
esp_err_t kaleidobox_nvs_set_instant_draw(bool enable);

// Gallery cycling: auto-advance (with interval_seconds) vs manual
// next/prev via the web app.
bool kaleidobox_nvs_get_gallery_auto_advance(void);
esp_err_t kaleidobox_nvs_set_gallery_auto_advance(bool enable);

uint16_t kaleidobox_nvs_get_gallery_interval_seconds(void);
esp_err_t kaleidobox_nvs_set_gallery_interval_seconds(uint16_t seconds);

// Whether kaleidoscope was running at last stop/start - kaleidoscope.c
// sets this on every start()/stop() call. Read at boot (see main.c) to
// resume the animation across a reboot instead of coming back up static.
bool kaleidobox_nvs_get_kaleido_running(void);
esp_err_t kaleidobox_nvs_set_kaleido_running(bool running);

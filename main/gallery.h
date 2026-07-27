#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// Saved-image list on the TF card (see sdcard.h) + cycling. Cycling mode
// (auto-advance with an interval, or manual next/prev) comes from
// settings.h's kaleidobox_nvs_get_gallery_auto_advance()/
// _gallery_interval_seconds(). Starts one background FreeRTOS task
// (gallery_init) that's a no-op whenever the TF card isn't mounted; it
// drives auto-advance AND the reboot-persistence autosave below off the
// same 1s tick, so there's just the one task for both concerns.
//
// Images live as raw CANVAS_WIDTH*CANVAS_HEIGHT*3 RGB888 dumps under
// /sdcard/gallery/<name>.raw - same format canvas.h already uses
// on-wire (GET /api/canvas, POST /api/canvas/submit), so no
// encode/decode step is needed to save or show one.

esp_err_t kaleidobox_gallery_init(void);

// Saves the current canvas/display buffer as a new gallery entry. name
// must be non-empty, contain no '/' or '.', and fit the gallery
// directory's path budget (see gallery.c) - rejected with
// ESP_ERR_INVALID_ARG otherwise. Untrusted input: reachable straight
// from the HTTP API (POST /api/gallery/save).
esp_err_t kaleidobox_gallery_save(const char *name);
esp_err_t kaleidobox_gallery_delete(const char *name);

// Writes up to max_names newline-joined entries into buf; returns count.
size_t kaleidobox_gallery_list(char *buf, size_t buf_size, size_t max_names);

esp_err_t kaleidobox_gallery_next(void);
esp_err_t kaleidobox_gallery_prev(void);

// Reads a saved entry's raw RGB888 bytes (exactly
// CANVAS_WIDTH*CANVAS_HEIGHT*3) into caller-provided buf, without
// touching the live canvas - used by the HTTP thumbnail endpoint
// (GET /api/gallery/image/<name>), unlike next()/prev() which display
// it. Same name validation/untrusted-input rules as save()/delete().
esp_err_t kaleidobox_gallery_read(const char *name, uint8_t *buf);

// Loads the auto-saved canvas snapshot (see gallery.c's background
// task) into the canvas buffer, if the TF card is mounted and a
// snapshot exists. Called once at boot (main.c) so a reboot restores
// whatever was last showing instead of coming back up blank.
esp_err_t kaleidobox_gallery_restore_state(void);

#pragma once

#include "esp_err.h"
#include <stddef.h>

// Saved-image list on the TF card (see sdcard.h) + cycling. Cycling mode
// (auto-advance with an interval, or manual next/prev) comes from
// settings.h's kaleidobox_nvs_get_gallery_auto_advance()/
// _gallery_interval_seconds(). Auto-advance runs as a FreeRTOS timer task
// while enabled and the gallery is the active display mode.
//
// STUB - not yet implemented.

esp_err_t kaleidobox_gallery_init(void);

// Saves the current canvas/display buffer as a new gallery entry.
esp_err_t kaleidobox_gallery_save(const char *name);
esp_err_t kaleidobox_gallery_delete(const char *name);

// Writes up to max_names newline-joined entries into buf; returns count.
size_t kaleidobox_gallery_list(char *buf, size_t buf_size, size_t max_names);

esp_err_t kaleidobox_gallery_next(void);
esp_err_t kaleidobox_gallery_prev(void);

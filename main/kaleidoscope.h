#pragma once

#include "esp_err.h"
#include "image_decode.h"
#include <stdbool.h>

// Per-frame polar-fold kaleidoscope transform: for each output pixel,
// compute angle+radius from center, fold the angle into the base wedge
// (kaleidobox_nvs_get_fold_count() repeats, mirroring alternate copies
// for the classic kaleidoscope look rather than a plain repeated pie
// slice), sample the source image via a rotating (and, if
// kaleidobox_nvs_get_motion_zoom() is set, zooming/breathing) offset
// that advances every frame. Runs as its own FreeRTOS task pushing
// frames to matrix.h (bulk draw_pixels, not per-pixel set_pixel - see
// matrix.h for why that matters) at ~25fps while active.
//
// v1 samples from whatever's on the 64x64 canvas (current drawing or
// last upload), not a separate higher-resolution source image - the
// original plan was to keep a bigger decoded image around specifically
// for this, but that adds real source-image-lifetime complexity for a
// quality improvement nobody's asked for yet. Revisit if the 64x64
// source visibly limits quality once this is actually being used.

esp_err_t kaleidobox_kaleidoscope_init(void);

// Sets the image the animation samples from (a decoded upload or the
// current canvas buffer) and starts the animation task.
esp_err_t kaleidobox_kaleidoscope_start(const kaleidobox_image_t *source);
void kaleidobox_kaleidoscope_stop(void);
bool kaleidobox_kaleidoscope_is_running(void);

// Swaps the live source image without stopping/restarting the
// animation - no task teardown/recreate, no precomputed pixel-table
// recalculation (those only depend on fold count, not source content),
// just a locked pointer swap. Returns ESP_ERR_INVALID_STATE if nothing
// is currently running (use start() instead). Meant for frequent calls
// from a live-editing path (e.g. instant-draw) where a full restart's
// black-frame flicker would be disruptive.
esp_err_t kaleidobox_kaleidoscope_update_source(const kaleidobox_image_t *source);

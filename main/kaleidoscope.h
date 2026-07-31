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

// Stops the animation and persists kaido_running=false to NVS - this
// IS the user's durable intent (Clear button, explicit stop request),
// so it should survive a reboot as "was off."
void kaleidobox_kaleidoscope_stop(void);

// Same task-stop mechanics as kaleidobox_kaleidoscope_stop() above, but
// does NOT touch the persisted kaleido_running flag - for internal
// callers (panel_takeover.c) pausing the animation for a transient
// PrintSpy/weather takeover, not because the user asked for it to stop.
// panel_takeover.c already tracks its own resume intent separately
// (snapshotted from NVS before calling this); using the persisting
// stop() here was a real bug once display-rotation started
// cycling every ~15-30s - a reflash landing mid-takeover would
// persist "not running" even though the user's actual last action was
// to leave it on (confirmed live: "every time I flash this thing now,
// it doesn't remember that it was already running the kaleidoscope").
void kaleidobox_kaleidoscope_stop_transient(void);

bool kaleidobox_kaleidoscope_is_running(void);

// Swaps the live source image without stopping/restarting the
// animation - no task teardown/recreate, no precomputed pixel-table
// recalculation (those only depend on fold count, not source content),
// just a locked pointer swap. Returns ESP_ERR_INVALID_STATE if nothing
// is currently running (use start() instead). Meant for frequent calls
// from a live-editing path (e.g. instant-draw) where a full restart's
// black-frame flicker would be disruptive.
esp_err_t kaleidobox_kaleidoscope_update_source(const kaleidobox_image_t *source);

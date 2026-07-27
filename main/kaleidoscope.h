#pragma once

#include "esp_err.h"
#include "image_decode.h"

// Per-frame polar-fold kaleidoscope transform: for each output pixel,
// compute angle+radius from center, fold the angle into the base wedge
// (kaleidobox_nvs_get_fold_count() repeats), sample the source image via
// a rotating (and, if kaleidobox_nvs_get_motion_zoom() is set,
// zooming/panning) UV offset that advances every frame. Runs as a
// FreeRTOS timer task pushing frames to matrix.h while active.
//
// STUB - not yet implemented. No FreeRTOS task is created yet; start/stop
// are no-ops.

esp_err_t kaleidobox_kaleidoscope_init(void);

// Sets the image the animation samples from (a decoded upload or the
// current canvas buffer) and starts the animation task.
esp_err_t kaleidobox_kaleidoscope_start(const kaleidobox_image_t *source);
void kaleidobox_kaleidoscope_stop(void);
bool kaleidobox_kaleidoscope_is_running(void);

#pragma once

#include "esp_err.h"
#include <stdint.h>

// The live draw-mode pixel buffer (64x64 RGB888). Mutated either
// pixel-by-pixel from /ws/draw messages (instant-draw mode) or in one
// shot from a full-grid POST to /api/canvas/submit (draw-then-submit
// mode) - see settings.h's kaleidobox_nvs_get_instant_draw(). Whichever
// mode is active, a completed change pushes the buffer to the matrix via
// matrix.h.
//
// STUB - not yet implemented.

#define CANVAS_WIDTH 64
#define CANVAS_HEIGHT 64

esp_err_t kaleidobox_canvas_init(void);
void kaleidobox_canvas_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                                 uint8_t b);
const uint8_t *kaleidobox_canvas_buffer(void); // CANVAS_WIDTH*CANVAS_HEIGHT*3 bytes, RGB888

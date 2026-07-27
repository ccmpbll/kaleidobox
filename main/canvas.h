#pragma once

#include "esp_err.h"
#include <stdint.h>

// The live draw-mode pixel buffer (64x64 RGB888). Mutated either from
// batched /ws/draw messages (instant-draw mode) or in one shot from a
// full-grid POST to /api/canvas/submit (draw-then-submit mode) - the
// client picks which by how it sends data, both paths already exist
// server-side. Each set_pixel() call also writes straight into the
// matrix driver (see matrix.h), live immediately - matrix.cpp runs
// single-buffered, so there's currently no separate "show it now" step.

#define CANVAS_WIDTH 64
#define CANVAS_HEIGHT 64

esp_err_t kaleidobox_canvas_init(void);
void kaleidobox_canvas_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                                 uint8_t b);
const uint8_t *kaleidobox_canvas_buffer(void); // CANVAS_WIDTH*CANVAS_HEIGHT*3 bytes, RGB888

// Replaces the entire grid in one shot (CANVAS_WIDTH*CANVAS_HEIGHT*3
// bytes, RGB888) - used for draw-then-submit and clear. Pushes via
// matrix.h's bulk draw call, not CANVAS_WIDTH*CANVAS_HEIGHT individual
// set_pixel() calls - see matrix.h for why that mattered.
void kaleidobox_canvas_set_all(const uint8_t *rgb888);

// Forwards to matrix.h's flip - currently a no-op (single-buffer mode,
// see matrix.cpp). Kept for kaleidoscope mode, which will re-enable
// double buffering and need this. Not called anywhere in draw mode.
void kaleidobox_canvas_flip(void);

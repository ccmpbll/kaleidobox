#pragma once

#include "esp_err.h"
#include <stdint.h>

// The live draw-mode pixel buffer (64x64 RGB888). Mutated either
// pixel-by-pixel from /ws/draw messages (instant-draw mode) or in one
// shot from a full-grid POST to /api/canvas/submit (draw-then-submit
// mode) - the client picks which by how it sends data, both paths already
// exist server-side. Each set_pixel() call also writes straight into the
// matrix driver's back buffer (see matrix.h) - callers decide when a
// complete update is ready to show by calling kaleidobox_canvas_flip()
// once (not per-pixel - http_server.c's handlers do this after either a
// single WS edit or a whole submitted grid).

#define CANVAS_WIDTH 64
#define CANVAS_HEIGHT 64

esp_err_t kaleidobox_canvas_init(void);
void kaleidobox_canvas_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                                 uint8_t b);
const uint8_t *kaleidobox_canvas_buffer(void); // CANVAS_WIDTH*CANVAS_HEIGHT*3 bytes, RGB888

// Pushes the matrix driver's back buffer live (double-buffered flip).
void kaleidobox_canvas_flip(void);

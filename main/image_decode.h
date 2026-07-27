#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Decodes an uploaded JPEG or PNG into an RGB888 buffer. JPEG decode is
// scaled down (via espressif/esp_jpeg's built-in power-of-two scaling)
// so a large photo doesn't fully decode at native resolution just to be
// downsampled to 64x64 immediately after - capped around 512px on the
// long edge, matching the "keep a few hundred px to sample from" plan
// for the future kaleidoscope mode, which will reuse this same decode
// path. PNG has no such built-in scaling (lodepng always decodes at
// native resolution), so oversized PNGs are rejected outright instead -
// see MAX_PNG_DIMENSION below.
typedef struct {
  uint8_t *rgb888; // caller-owned, free with kaleidobox_image_free()
  uint16_t width;
  uint16_t height;
} kaleidobox_image_t;

esp_err_t kaleidobox_image_decode(const uint8_t *data, size_t len,
                                  kaleidobox_image_t *out);
void kaleidobox_image_free(kaleidobox_image_t *img);

// Box-filter downsamples src to exactly CANVAS_WIDTH x CANVAS_HEIGHT
// RGB888 into dst (caller-owned, CANVAS_WIDTH*CANVAS_HEIGHT*3 bytes).
// Box-filter (area average), not nearest-neighbor - src is typically
// several times larger than the 64x64 target, and averaging looks
// meaningfully less noisy for photo content than picking one sample
// pixel per cell.
//
// No dithering here despite earlier planning notes mentioning
// Floyd-Steinberg - that assumed the panel might need reduced color
// depth. It doesn't: the HUB75 driver runs 8-bit BCM per channel (see
// the boot log), i.e. genuine 24-bit color per pixel, so there's no
// palette/depth quantization happening for dithering to help with. The
// real quality lever for downsampling this hard is the box filter
// itself.
void kaleidobox_image_resize_to_canvas(const kaleidobox_image_t *src,
                                       uint8_t *dst);

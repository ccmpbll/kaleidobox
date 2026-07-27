#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// Decodes an uploaded JPEG or PNG into an RGB888 buffer sized to the
// source's own dimensions (capped - see image_decode.c), for the
// kaleidoscope transform and canvas display to sample from. JPEG via the
// espressif/esp_jpeg component (supports scaled decode, so we're not
// forced to fully decode a huge photo just to end up sampling a tiny
// region of it). PNG via vendored lodepng.c/.h (no ESP-IDF component
// exists for PNG; lodepng is small and permissively licensed).
//
// STUB - not yet implemented.

typedef struct {
  uint8_t *rgb888; // caller-owned, free with kaleidobox_image_free()
  uint16_t width;
  uint16_t height;
} kaleidobox_image_t;

esp_err_t kaleidobox_image_decode(const uint8_t *data, size_t len,
                                  kaleidobox_image_t *out);
void kaleidobox_image_free(kaleidobox_image_t *img);

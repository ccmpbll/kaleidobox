#include "image_decode.h"

#include "canvas.h"
#include "esp_log.h"
#include "jpeg_decoder.h"
#include "lodepng.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "image_decode";

// Target cap for JPEG decode's long edge - matches the "keep a few
// hundred px, don't need full native res" plan (see image_decode.h).
#define JPEG_MAX_DIMENSION 512

// PNG has no built-in scaled decode (lodepng always decodes at native
// resolution) - reject outright above this rather than risk a huge
// allocation. 1536x1536 RGB888 is ~7MB, comfortable inside 16MB PSRAM.
#define PNG_MAX_DIMENSION 1536

static bool is_jpeg(const uint8_t *data, size_t len) {
  return len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

static bool is_png(const uint8_t *data, size_t len) {
  static const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return len >= 8 && memcmp(data, sig, 8) == 0;
}

static esp_err_t decode_jpeg(const uint8_t *data, size_t len,
                             kaleidobox_image_t *out) {
  esp_jpeg_image_cfg_t cfg = {0};
  cfg.indata = (uint8_t *)data;
  cfg.indata_size = len;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB888;
  cfg.out_scale = JPEG_IMAGE_SCALE_0;

  esp_jpeg_image_output_t info = {0};
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) {
    ESP_LOGW(TAG, "esp_jpeg_get_image_info failed - not a valid JPEG?");
    return ESP_FAIL;
  }

  uint16_t native_max = info.width > info.height ? info.width : info.height;
  if (native_max <= JPEG_MAX_DIMENSION) {
    cfg.out_scale = JPEG_IMAGE_SCALE_0;
  } else if (native_max <= JPEG_MAX_DIMENSION * 2) {
    cfg.out_scale = JPEG_IMAGE_SCALE_1_2;
  } else if (native_max <= JPEG_MAX_DIMENSION * 4) {
    cfg.out_scale = JPEG_IMAGE_SCALE_1_4;
  } else {
    cfg.out_scale = JPEG_IMAGE_SCALE_1_8;
  }

  // Re-query at the chosen scale - output_len/width/height above were
  // for SCALE_0 (native), not necessarily what we're about to decode.
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) {
    ESP_LOGW(TAG, "esp_jpeg_get_image_info failed at chosen scale");
    return ESP_FAIL;
  }

  uint8_t *outbuf = malloc(info.output_len);
  if (!outbuf) {
    return ESP_ERR_NO_MEM;
  }
  cfg.outbuf = outbuf;
  cfg.outbuf_size = info.output_len;

  esp_jpeg_image_output_t result = {0};
  esp_err_t err = esp_jpeg_decode(&cfg, &result);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_jpeg_decode failed: %s", esp_err_to_name(err));
    free(outbuf);
    return err;
  }

  out->rgb888 = outbuf;
  out->width = result.width;
  out->height = result.height;
  ESP_LOGI(TAG, "decoded JPEG: %ux%u (native max edge %u, scale index %d)",
          result.width, result.height, native_max, (int)cfg.out_scale);
  return ESP_OK;
}

static esp_err_t decode_png(const uint8_t *data, size_t len,
                            kaleidobox_image_t *out) {
  LodePNGState state;
  lodepng_state_init(&state);

  unsigned w = 0, h = 0;
  unsigned lp_err = lodepng_inspect(&w, &h, &state, data, len);
  lodepng_state_cleanup(&state);
  if (lp_err) {
    ESP_LOGW(TAG, "lodepng_inspect failed (%u) - not a valid PNG?", lp_err);
    return ESP_FAIL;
  }
  if (w > PNG_MAX_DIMENSION || h > PNG_MAX_DIMENSION) {
    ESP_LOGW(TAG, "PNG %ux%u exceeds %dx%d max - reject (PNG can't be "
                  "scale-decoded like JPEG, use a smaller image)",
            w, h, PNG_MAX_DIMENSION, PNG_MAX_DIMENSION);
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t *rgb888 = NULL;
  lp_err = lodepng_decode24(&rgb888, &w, &h, data, len);
  if (lp_err) {
    ESP_LOGW(TAG, "lodepng_decode24 failed: %u", lp_err);
    return ESP_FAIL;
  }

  out->rgb888 = rgb888;
  out->width = (uint16_t)w;
  out->height = (uint16_t)h;
  ESP_LOGI(TAG, "decoded PNG: %ux%u", w, h);
  return ESP_OK;
}

esp_err_t kaleidobox_image_decode(const uint8_t *data, size_t len,
                                  kaleidobox_image_t *out) {
  out->rgb888 = NULL;
  out->width = 0;
  out->height = 0;

  if (is_jpeg(data, len)) {
    return decode_jpeg(data, len, out);
  }
  if (is_png(data, len)) {
    return decode_png(data, len, out);
  }
  ESP_LOGW(TAG, "unrecognized image format (not JPEG or PNG magic bytes)");
  return ESP_ERR_NOT_SUPPORTED;
}

void kaleidobox_image_free(kaleidobox_image_t *img) {
  if (img && img->rgb888) {
    free(img->rgb888);
    img->rgb888 = NULL;
  }
}

void kaleidobox_image_resize_to_canvas(const kaleidobox_image_t *src,
                                       uint8_t *dst) {
  for (int dy = 0; dy < CANVAS_HEIGHT; dy++) {
    int sy0 = (dy * src->height) / CANVAS_HEIGHT;
    int sy1 = ((dy + 1) * src->height) / CANVAS_HEIGHT;
    if (sy1 <= sy0) {
      sy1 = sy0 + 1;
    }
    for (int dx = 0; dx < CANVAS_WIDTH; dx++) {
      int sx0 = (dx * src->width) / CANVAS_WIDTH;
      int sx1 = ((dx + 1) * src->width) / CANVAS_WIDTH;
      if (sx1 <= sx0) {
        sx1 = sx0 + 1;
      }

      uint32_t r = 0, g = 0, b = 0, count = 0;
      for (int sy = sy0; sy < sy1 && sy < src->height; sy++) {
        for (int sx = sx0; sx < sx1 && sx < src->width; sx++) {
          size_t idx = ((size_t)sy * src->width + sx) * 3;
          r += src->rgb888[idx];
          g += src->rgb888[idx + 1];
          b += src->rgb888[idx + 2];
          count++;
        }
      }

      size_t dst_idx = ((size_t)dy * CANVAS_WIDTH + dx) * 3;
      if (count == 0) {
        dst[dst_idx] = dst[dst_idx + 1] = dst[dst_idx + 2] = 0;
      } else {
        dst[dst_idx] = (uint8_t)(r / count);
        dst[dst_idx + 1] = (uint8_t)(g / count);
        dst[dst_idx + 2] = (uint8_t)(b / count);
      }
    }
  }
}

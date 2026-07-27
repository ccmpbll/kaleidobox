#include "image_decode.h"

#include "canvas.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "jerror.h"
#include "jpeglib.h"
#include "lodepng.h"
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "image_decode";

// Target cap for JPEG decode's long edge - matches the "keep a few
// hundred px, don't need full native res" plan (see image_decode.h).
#define JPEG_MAX_DIMENSION 512

// PNG has no built-in scaled decode (lodepng always decodes at native
// resolution) - reject outright above this rather than risk a huge
// allocation. A total-pixel-count cap, not a per-dimension one - an
// earlier per-dimension 1536 cap rejected real phone screenshots (e.g.
// 1170x2532, well under 1536 wide but taller than that).
//
// This number is empirically measured, not computed from a memory
// model - an earlier 4,000,000 cap (based on estimating only the final
// RGB888 buffer) turned out to still be too permissive: lodepng_decode24
// keeps the image in its native decoded color format AND a separate
// converted RGB888 buffer alive at the same time, so peak usage is
// meaningfully more than just width*height*3. Binary-searched on real
// hardware: 1,414x1,414 (~2.0MP) decodes fine, 1,600x1,400 (~2.24MP)
// fails with lodepng error 83 ("memory allocation failed") despite
// 16MB+ PSRAM free at the time - the two buffers alive at once just
// don't fit. Capped here with a bit of margin below the confirmed
// failure point.
#define PNG_MAX_PIXELS 2000000u

static bool is_jpeg(const uint8_t *data, size_t len) {
  return len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
}

static bool is_png(const uint8_t *data, size_t len) {
  static const uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  return len >= 8 && memcmp(data, sig, 8) == 0;
}

// Custom libjpeg error handler using setjmp/longjmp to safely unwind out
// of a decode on error. Espressif's own hello_jpeg example (the
// reference for this integration) has a real bug here: its
// my_error_exit() logs and *returns* instead of longjmp-ing, which
// leaves libjpeg's internals continuing to execute after what was
// supposed to be a fatal error - undefined behavior. Not repeating that.
struct kaleidobox_jpeg_error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

static void kaleidobox_jpeg_error_exit(j_common_ptr cinfo) {
  struct kaleidobox_jpeg_error_mgr *err =
      (struct kaleidobox_jpeg_error_mgr *)cinfo->err;
  char msg[JMSG_LENGTH_MAX];
  (*cinfo->err->format_message)(cinfo, msg);
  ESP_LOGW(TAG, "libjpeg error: %s", msg);
  longjmp(err->setjmp_buffer, 1);
}

static esp_err_t decode_jpeg(const uint8_t *data, size_t len,
                             kaleidobox_image_t *out) {
  struct jpeg_decompress_struct cinfo = {0};
  struct kaleidobox_jpeg_error_mgr jerr;
  uint8_t *outbuf = NULL;

  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = kaleidobox_jpeg_error_exit;
  if (setjmp(jerr.setjmp_buffer)) {
    // Landed here via longjmp from kaleidobox_jpeg_error_exit - some
    // libjpeg call above failed. Corrupt file, unsupported variant
    // (12-bit, CMYK, etc.), or similar - not a size/memory issue,
    // those are handled separately below.
    jpeg_destroy_decompress(&cinfo);
    if (outbuf) {
      free(outbuf);
    }
    return ESP_ERR_NOT_SUPPORTED;
  }

  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, data, len);
  jpeg_read_header(&cinfo, TRUE);

  uint16_t native_max = cinfo.image_width > cinfo.image_height
                            ? cinfo.image_width
                            : cinfo.image_height;
  cinfo.scale_num = 1;
  if (native_max <= JPEG_MAX_DIMENSION) {
    cinfo.scale_denom = 1;
  } else if (native_max <= JPEG_MAX_DIMENSION * 2) {
    cinfo.scale_denom = 2;
  } else if (native_max <= JPEG_MAX_DIMENSION * 4) {
    cinfo.scale_denom = 4;
  } else {
    cinfo.scale_denom = 8;
  }
  cinfo.out_color_space = JCS_RGB; // uniform RGB888 output regardless of
                                   // source color space (grayscale, YCbCr, ...)

  jpeg_start_decompress(&cinfo); // output_width/height/components now valid

  size_t row_stride = (size_t)cinfo.output_width * cinfo.output_components;
  outbuf = malloc(row_stride * cinfo.output_height);
  if (!outbuf) {
    jpeg_destroy_decompress(&cinfo);
    return ESP_ERR_NO_MEM;
  }

  JSAMPROW row_pointer[1];
  while (cinfo.output_scanline < cinfo.output_height) {
    row_pointer[0] = outbuf + (size_t)cinfo.output_scanline * row_stride;
    jpeg_read_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  out->rgb888 = outbuf;
  out->width = (uint16_t)cinfo.output_width;
  out->height = (uint16_t)cinfo.output_height;
  ESP_LOGI(TAG, "decoded JPEG: %ux%u (native max edge %u, scale 1/%u, "
                "progressive=%d)",
          cinfo.output_width, cinfo.output_height, native_max,
          (unsigned)cinfo.scale_denom, cinfo.progressive_mode);
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
  if ((uint32_t)w * (uint32_t)h > PNG_MAX_PIXELS) {
    ESP_LOGW(TAG, "PNG %ux%u (%u px) exceeds %u px max - reject (PNG can't "
                  "be scale-decoded like JPEG, use a smaller image)",
            w, h, w * h, PNG_MAX_PIXELS);
    return ESP_ERR_INVALID_SIZE;
  }

  ESP_LOGI(TAG, "pre-decode heap: internal free=%u, psram free=%u",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  uint8_t *rgb888 = NULL;
  lp_err = lodepng_decode24(&rgb888, &w, &h, data, len);
  if (lp_err) {
    ESP_LOGW(TAG, "lodepng_decode24 failed: %u", lp_err);
    // 83 = "memory allocation failed" - lodepng keeps the native-decoded
    // buffer and the converted RGB888 buffer alive at the same time, so
    // an image can still exceed real available memory even under
    // PNG_MAX_PIXELS if the system happens to have less free than usual
    // at that moment. Map to the same "PNG too large" client message as
    // the early size-reject case rather than a generic decode error -
    // still accurate and actionable.
    return lp_err == 83 ? ESP_ERR_INVALID_SIZE : ESP_FAIL;
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
  // Distinct from decode_jpeg's ESP_ERR_NOT_SUPPORTED (a recognized JPEG
  // the decoder can't parse, likely progressive) - this is "not even
  // JPEG or PNG to begin with", so http_server.c can give a different,
  // more accurate message for each.
  ESP_LOGW(TAG, "unrecognized image format (not JPEG or PNG magic bytes)");
  return ESP_ERR_NOT_FOUND;
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

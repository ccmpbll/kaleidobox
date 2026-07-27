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
#define JPEG_DECODE_MAX_DIMENSION 512

// Progressive JPEG only - see the check in decode_jpeg() for why baseline
// doesn't need this. Binary-searched on real hardware: 4.7MP (4,687,500px)
// decodes fine, 5.3MP fails with libjpeg's own out-of-memory error. Capped
// meaningfully below the confirmed-good point, not right at it - free heap
// varies run to run with server uptime/fragmentation, so a cap that just
// barely fit once isn't reliably safe on every request.
#define JPEG_PROGRESSIVE_MAX_PIXELS 4500000u

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

// Phones store portrait photos as landscape pixel data plus an EXIF
// Orientation tag telling viewers how to rotate on display - libjpeg
// only decodes raw pixels, it doesn't know or care about EXIF, so
// without this every portrait upload lands sideways. Walks the raw
// APP1/EXIF marker libjpeg was told to keep (via jpeg_save_markers(),
// called before jpeg_read_header() below) to find tag 0x0112
// (Orientation) in IFD0. Every offset is bounds-checked against the
// marker's actual data_length - this is untrusted data straight from
// an uploaded file. Returns 1 (normal/no-op) if the marker's missing,
// malformed, or the tag isn't present - same as a real photo with no
// EXIF at all.
static uint16_t exif_u16(const uint8_t *p, bool le) {
  return le ? (uint16_t)(p[0] | (p[1] << 8)) : (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t exif_u32(const uint8_t *p, bool le) {
  if (le) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
  }
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int read_exif_orientation(jpeg_saved_marker_ptr marker_list) {
  for (jpeg_saved_marker_ptr m = marker_list; m; m = m->next) {
    if (m->marker != JPEG_APP0 + 1 || m->data_length < 14) {
      continue;
    }
    if (memcmp(m->data, "Exif\0\0", 6) != 0) {
      continue;
    }
    const uint8_t *tiff = m->data + 6;
    size_t tiff_len = m->data_length - 6;
    if (tiff_len < 8) {
      continue;
    }

    bool little_endian;
    if (memcmp(tiff, "II", 2) == 0) {
      little_endian = true;
    } else if (memcmp(tiff, "MM", 2) == 0) {
      little_endian = false;
    } else {
      continue;
    }

    uint32_t ifd0_offset = exif_u32(tiff + 4, little_endian);
    if ((uint64_t)ifd0_offset + 2 > tiff_len) {
      continue;
    }
    uint16_t entry_count = exif_u16(tiff + ifd0_offset, little_endian);
    uint32_t entries_start = ifd0_offset + 2;
    if ((uint64_t)entries_start + (uint64_t)entry_count * 12 > tiff_len) {
      continue;
    }

    for (uint16_t i = 0; i < entry_count; i++) {
      const uint8_t *entry = tiff + entries_start + (size_t)i * 12;
      uint16_t tag = exif_u16(entry, little_endian);
      if (tag != 0x0112) {
        continue;
      }
      uint16_t type = exif_u16(entry + 2, little_endian);
      if (type != 3) { // SHORT - the only type Orientation is ever encoded as
        break;
      }
      int value = exif_u16(entry + 8, little_endian); // first 2 bytes of the value field
      return (value >= 1 && value <= 8) ? value : 1;
    }
  }
  return 1; // no EXIF, no orientation tag, or unparseable - treat as normal
}

// EXIF Orientation 2-8 as in-place-size rotate/flip transforms on an
// RGB888 buffer. 1 (normal) needs no transform and isn't handled here.
// Dimensions swap for 5/6/7/8 (rotate 90) - caller must swap width/height
// alongside calling these.
static void rot_flip_h(const uint8_t *src, int w, int h, uint8_t *dst) {
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      memcpy(&dst[(y * w + (w - 1 - x)) * 3], &src[(y * w + x) * 3], 3);
    }
  }
}

static void rot_flip_v(const uint8_t *src, int w, int h, uint8_t *dst) {
  for (int y = 0; y < h; y++) {
    memcpy(&dst[(size_t)(h - 1 - y) * w * 3], &src[(size_t)y * w * 3], (size_t)w * 3);
  }
}

static void rot_180(const uint8_t *src, int w, int h, uint8_t *dst) {
  size_t n = (size_t)w * h;
  for (size_t i = 0; i < n; i++) {
    memcpy(&dst[(n - 1 - i) * 3], &src[i * 3], 3);
  }
}

// dst must be sized h x w (swapped) - src top-left ends up at dst top-right.
static void rot_90cw(const uint8_t *src, int w, int h, uint8_t *dst) {
  int dst_w = h;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int dx = h - 1 - y, dy = x;
      memcpy(&dst[(size_t)(dy * dst_w + dx) * 3], &src[(size_t)(y * w + x) * 3], 3);
    }
  }
}

// dst must be sized h x w (swapped) - src top-left ends up at dst bottom-left.
static void rot_90ccw(const uint8_t *src, int w, int h, uint8_t *dst) {
  int dst_w = h;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int dx = y, dy = w - 1 - x;
      memcpy(&dst[(size_t)(dy * dst_w + dx) * 3], &src[(size_t)(y * w + x) * 3], 3);
    }
  }
}

// Applies the correction for the given EXIF orientation value to *buf
// (RGB888, *w x *h), replacing *buf with a freshly allocated corrected
// buffer and updating *w/*h if dimensions swapped. No-op for
// orientation 1 (normal) or anything out of the defined 1-8 range.
static void apply_exif_orientation(uint8_t **buf, uint16_t *w, uint16_t *h,
                                   int orientation) {
  if (orientation <= 1 || orientation > 8) {
    return;
  }
  int sw = *w, sh = *h;
  bool swaps_dims = orientation >= 5; // 5,6,7,8 all rotate 90 either way
  size_t out_size = (size_t)sw * sh * 3;
  uint8_t *out = malloc(out_size);
  if (!out) {
    ESP_LOGW(TAG, "no memory to correct EXIF orientation %d - leaving image "
                  "as-is",
            orientation);
    return;
  }

  switch (orientation) {
  case 2: rot_flip_h(*buf, sw, sh, out); break;
  case 3: rot_180(*buf, sw, sh, out); break;
  case 4: rot_flip_v(*buf, sw, sh, out); break;
  case 5: { // transpose: flip horizontal, then rotate 90 CW
    uint8_t *tmp = malloc(out_size);
    if (!tmp) { free(out); return; }
    rot_flip_h(*buf, sw, sh, tmp);
    rot_90cw(tmp, sw, sh, out);
    free(tmp);
    break;
  }
  case 6: rot_90cw(*buf, sw, sh, out); break;
  case 7: { // transverse: flip horizontal, then rotate 90 CCW
    uint8_t *tmp = malloc(out_size);
    if (!tmp) { free(out); return; }
    rot_flip_h(*buf, sw, sh, tmp);
    rot_90ccw(tmp, sw, sh, out);
    free(tmp);
    break;
  }
  case 8: rot_90ccw(*buf, sw, sh, out); break;
  }

  free(*buf);
  *buf = out;
  if (swaps_dims) {
    *w = (uint16_t)sh;
    *h = (uint16_t)sw;
  }
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
  // Keep the raw APP1/EXIF marker around so we can read the Orientation
  // tag below - must be called before jpeg_read_header(), which is where
  // libjpeg actually scans and collects markers.
  jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF);
  jpeg_mem_src(&cinfo, data, len);
  jpeg_read_header(&cinfo, TRUE);
  // marker_list is only valid until jpeg_destroy_decompress() frees it -
  // read it now, use the plain int later.
  int exif_orientation = read_exif_orientation(cinfo.marker_list);

  // Unlike baseline (decoded scanline-by-scanline, so scale_denom genuinely
  // bounds peak memory), progressive JPEG requires buffering every DCT
  // coefficient for the WHOLE image before any output can be produced -
  // scan order refines coefficients across the full image over multiple
  // passes, so there's no way to decode a strip at a time. That buffer's
  // size is driven by native resolution, not the scaled output size we
  // actually want. Binary-searched the real ceiling on this board (16MB
  // PSRAM, shared with everything else already resident): 4.7MP progressive
  // decodes fine, 5.3MP fails with libjpeg's own "Insufficient memory"
  // error. Capped with margin below the confirmed failure point - same
  // methodology as PNG_MAX_PIXELS below. Baseline JPEGs have no such cap;
  // scale_denom already bounds them.
  if (cinfo.progressive_mode &&
      (uint32_t)cinfo.image_width * cinfo.image_height > JPEG_PROGRESSIVE_MAX_PIXELS) {
    ESP_LOGW(TAG, "progressive JPEG %ux%u (%u px) exceeds %u px max - "
                  "progressive decode needs the full-resolution coefficient "
                  "buffer regardless of output scale, unlike baseline",
            cinfo.image_width, cinfo.image_height,
            cinfo.image_width * cinfo.image_height, JPEG_PROGRESSIVE_MAX_PIXELS);
    jpeg_destroy_decompress(&cinfo);
    return ESP_ERR_INVALID_SIZE;
  }

  uint16_t native_max = cinfo.image_width > cinfo.image_height
                            ? cinfo.image_width
                            : cinfo.image_height;
  cinfo.scale_num = 1;
  if (native_max <= JPEG_DECODE_MAX_DIMENSION) {
    cinfo.scale_denom = 1;
  } else if (native_max <= JPEG_DECODE_MAX_DIMENSION * 2) {
    cinfo.scale_denom = 2;
  } else if (native_max <= JPEG_DECODE_MAX_DIMENSION * 4) {
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

  uint16_t out_w = (uint16_t)cinfo.output_width;
  uint16_t out_h = (uint16_t)cinfo.output_height;
  int progressive = cinfo.progressive_mode;
  jpeg_destroy_decompress(&cinfo); // frees marker_list - already read above

  // outbuf here is always at the small scaled-down output size (a few
  // hundred px), regardless of source resolution or progressive/baseline -
  // scale_denom already bounds it. Rotating a buffer this size is cheap,
  // no need to worry about it colliding with the memory limits above.
  apply_exif_orientation(&outbuf, &out_w, &out_h, exif_orientation);

  out->rgb888 = outbuf;
  out->width = out_w;
  out->height = out_h;
  ESP_LOGI(TAG, "decoded JPEG: %ux%u (native max edge %u, scale 1/%u, "
                "progressive=%d, exif_orientation=%d)",
          out_w, out_h, native_max, (unsigned)cinfo.scale_denom, progressive,
          exif_orientation);
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

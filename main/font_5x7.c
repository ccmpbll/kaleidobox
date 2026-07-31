#include "font_5x7.h"

#include "matrix.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// 5x7 bitmap font, ASCII 0x20 (space) - 0x7E ('~'), vendored from
// Waveshare's official ESP32-S3-RGB-Matrix example
// (github.com/waveshareteam/ESP32-S3-RGB-Matrix,
// example/idf_v5.5.2/components/font/font_5x7.c - same repo this
// board's HUB75 pinout was verified against), Apache-2.0 licensed.
// That component targets LVGL (variable-width glyph format, needs the
// whole LVGL + bsp_display stack this project doesn't use); this
// vendors just the raw glyph pixel bytes and drops the LVGL wrapper
// entirely, for status/IP display via matrix.h directly.
//
// Real structure, verified byte-by-byte against the source file (not
// assumed): 95 glyphs, one 7-byte block per glyph (1 byte/row, top 5
// bits = pixel columns MSB-first, bottom 3 bits unused) EXCEPT 5
// descender characters (g, j, p, q, y) which have an 8th row in the
// source for the tail below the baseline. kaleidobox_font_draw_text()
// only draws the top 7 rows uniformly for every glyph - a fixed-height
// renderer is simpler and the actual cost is just those 5 letters
// losing their descender tail (still legible) - but glyph_offset()
// still walks past each real 7-or-8-byte block correctly so no later
// glyph's data gets misaligned.
static const uint8_t glyph_bitmap[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* 0x20 ' ' (5x7) - 5 bytes (1 byte per row, 5 bits valid) */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x20,  /* 0x21 '!' */
    0x50, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00,  /* 0x22 '"' */
    0x50, 0x50, 0xF8, 0x50, 0xF8, 0x50, 0x50,  /* 0x23 '#' */
    0x20, 0x78, 0xA0, 0x70, 0x28, 0xF0, 0x20,  /* 0x24 '$' */
    0xC0, 0xC8, 0x10, 0x20, 0x40, 0x98, 0x18,  /* 0x25 '%' */
    0x40, 0xA0, 0xA0, 0x40, 0xA8, 0x90, 0x68,  /* 0x26 '&' */
    0x60, 0x20, 0x40, 0x00, 0x00, 0x00, 0x00,  /* 0x27 ''' */
    0x10, 0x20, 0x40, 0x40, 0x40, 0x20, 0x10,  /* 0x28 '(' */
    0x40, 0x20, 0x10, 0x10, 0x10, 0x20, 0x40,  /* 0x29 ')' */
    0x00, 0x20, 0xA8, 0x70, 0xA8, 0x20, 0x00,  /* 0x2A '*' */
    0x00, 0x20, 0x20, 0xF8, 0x20, 0x20, 0x00,  /* 0x2B '+' */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x20,  /* 0x2C ',' */
    0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00,  /* 0x2D '-' */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60,  /* 0x2E '.' */
    0x00, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00,  /* 0x2F '/' */
    0x70, 0x88, 0x98, 0xA8, 0xC8, 0x88, 0x70,  /* 0x30 '0' */
    0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70,  /* 0x31 '1' */
    0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xF8,  /* 0x32 '2' */
    0x70, 0x88, 0x08, 0x30, 0x08, 0x88, 0x70,  /* 0x33 '3' */
    0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10,  /* 0x34 '4' */
    0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70,  /* 0x35 '5' */
    0x30, 0x40, 0x80, 0xF0, 0x88, 0x88, 0x70,  /* 0x36 '6' */
    0xF8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40,  /* 0x37 '7' */
    0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70,  /* 0x38 '8' */
    0x70, 0x88, 0x88, 0x78, 0x08, 0x10, 0x60,  /* 0x39 '9' */
    0x00, 0x60, 0x60, 0x00, 0x60, 0x60, 0x00,  /* 0x3A ':' */
    0x00, 0x60, 0x60, 0x00, 0x60, 0x20, 0x40,  /* 0x3B ';' */
    0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08,  /* 0x3C '<' */
    0x00, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00,  /* 0x3D '=' */
    0x40, 0x20, 0x10, 0x08, 0x10, 0x20, 0x40,  /* 0x3E '>' */
    0x70, 0x88, 0x08, 0x10, 0x20, 0x00, 0x20,  /* 0x3F '?' */
    0x70, 0x88, 0x08, 0x68, 0xA8, 0xA8, 0x70,  /* 0x40 '@' */
    0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88,  /* 0x41 'A' */
    0xF0, 0x88, 0x88, 0xF0, 0x88, 0x88, 0xF0,  /* 0x42 'B' */
    0x70, 0x88, 0x80, 0x80, 0x80, 0x88, 0x70,  /* 0x43 'C' */
    0xF0, 0x88, 0x88, 0x88, 0x88, 0x88, 0xF0,  /* 0x44 'D' */
    0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0xF8,  /* 0x45 'E' */
    0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80,  /* 0x46 'F' */
    0x70, 0x88, 0x80, 0x80, 0x98, 0x88, 0x70,  /* 0x47 'G' */
    0x88, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88,  /* 0x48 'H' */
    0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70,  /* 0x49 'I' */
    0x08, 0x08, 0x08, 0x08, 0x08, 0x88, 0x70,  /* 0x4A 'J' */
    0x88, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x88,  /* 0x4B 'K' */
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xF8,  /* 0x4C 'L' */
    0x88, 0xD8, 0xA8, 0xA8, 0x88, 0x88, 0x88,  /* 0x4D 'M' */
    0x88, 0x88, 0xC8, 0xA8, 0x98, 0x88, 0x88,  /* 0x4E 'N' */
    0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70,  /* 0x4F 'O' */
    0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80, 0x80,  /* 0x50 'P' */
    0x70, 0x88, 0x88, 0x88, 0xA8, 0x90, 0x68,  /* 0x51 'Q' */
    0xF0, 0x88, 0x88, 0xF0, 0xA0, 0x90, 0x88,  /* 0x52 'R' */
    0x70, 0x88, 0x80, 0x70, 0x08, 0x88, 0x70,  /* 0x53 'S' */
    0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,  /* 0x54 'T' */
    0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70,  /* 0x55 'U' */
    0x88, 0x88, 0x88, 0x88, 0x88, 0x50, 0x20,  /* 0x56 'V' */
    0x88, 0x88, 0x88, 0xA8, 0xA8, 0xD8, 0x88,  /* 0x57 'W' */
    0x88, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88,  /* 0x58 'X' */
    0x88, 0x88, 0x88, 0x70, 0x20, 0x20, 0x20,  /* 0x59 'Y' */
    0xF8, 0x08, 0x10, 0x20, 0x40, 0x80, 0xF8,  /* 0x5A 'Z' */
    0x70, 0x40, 0x40, 0x40, 0x40, 0x40, 0x70,  /* 0x5B '[' */
    0x00, 0x80, 0x40, 0x20, 0x10, 0x08, 0x00,  /* 0x5C '\' */
    0x70, 0x10, 0x10, 0x10, 0x10, 0x10, 0x70,  /* 0x5D ']' */
    0x20, 0x50, 0x88, 0x00, 0x00, 0x00, 0x00,  /* 0x5E '^' */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8,  /* 0x5F '_' */
    0x40, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00,  /* 0x60 '`' */
    0x00, 0x00, 0x70, 0x08, 0x78, 0x88, 0x78,  /* 0x61 'a' */
    0x80, 0x80, 0xB0, 0xC8, 0x88, 0xC8, 0xB0,  /* 0x62 'b' */
    0x00, 0x00, 0x70, 0x88, 0x80, 0x88, 0x70,  /* 0x63 'c' */
    0x08, 0x08, 0x68, 0x98, 0x88, 0x98, 0x68,  /* 0x64 'd' */
    0x00, 0x00, 0x70, 0x88, 0xF8, 0x80, 0x70,  /* 0x65 'e' */
    0x30, 0x40, 0xE0, 0x40, 0x40, 0x40, 0x40,  /* 0x66 'f' */
    0x00, 0x00, 0x78, 0x88, 0x88, 0x78, 0x08, 0x70,  /* 0x67 'g' */
    0x80, 0x80, 0xB0, 0xC8, 0x88, 0x88, 0x88,  /* 0x68 'h' */
    0x20, 0x00, 0x60, 0x20, 0x20, 0x20, 0x70,  /* 0x69 'i' */
    0x10, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x60,  /* 0x6A 'j' */
    0x80, 0x80, 0x90, 0xA0, 0xC0, 0xA0, 0x90,  /* 0x6B 'k' */
    0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70,  /* 0x6C 'l' */
    0x00, 0x00, 0xD0, 0xA8, 0xA8, 0xA8, 0x88,  /* 0x6D 'm' */
    0x00, 0x00, 0xB0, 0xC8, 0x88, 0x88, 0x88,  /* 0x6E 'n' */
    0x00, 0x00, 0x70, 0x88, 0x88, 0x88, 0x70,  /* 0x6F 'o' */
    0x00, 0x00, 0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80,  /* 0x70 'p' */
    0x00, 0x00, 0x68, 0x98, 0x88, 0x98, 0x68, 0x08,  /* 0x71 'q' */
    0x00, 0x00, 0xB0, 0xC8, 0x80, 0x80, 0x80,  /* 0x72 'r' */
    0x00, 0x00, 0x70, 0x80, 0x70, 0x08, 0x70,  /* 0x73 's' */
    0x40, 0x40, 0xE0, 0x40, 0x40, 0x48, 0x30,  /* 0x74 't' */
    0x00, 0x00, 0x88, 0x88, 0x88, 0x98, 0x68,  /* 0x75 'u' */
    0x00, 0x00, 0x88, 0x88, 0x88, 0x50, 0x20,  /* 0x76 'v' */
    0x00, 0x00, 0x88, 0x88, 0xA8, 0xA8, 0x50,  /* 0x77 'w' */
    0x00, 0x00, 0x88, 0x50, 0x20, 0x50, 0x88,  /* 0x78 'x' */
    0x00, 0x00, 0x88, 0x88, 0x78, 0x08, 0x70, 0x00,  /* 0x79 'y' */
    0x00, 0x00, 0xF8, 0x10, 0x20, 0x40, 0xF8,  /* 0x7A 'z' */
    0x10, 0x20, 0x20, 0x40, 0x20, 0x20, 0x10,  /* 0x7B '{' */
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,  /* 0x7C '|' */
    0x40, 0x20, 0x20, 0x10, 0x20, 0x20, 0x40,  /* 0x7D '}' */
    0x00, 0x48, 0xB0, 0x00, 0x00, 0x00, 0x00,  /* 0x7E '~' */
};

static bool is_descender(char c) {
  return c == 'g' || c == 'j' || c == 'p' || c == 'q' || c == 'y';
}

// Byte offset of `c`'s glyph in glyph_bitmap - just walks the real
// per-glyph block sizes (7, or 8 for the 5 descenders) up to `c`.
// Called at most a few dozen times per short status string, not
// per-frame, so the O(n) walk isn't worth precomputing a table for.
static const uint8_t *glyph_ptr(char c) {
  if (c < 0x20 || c > 0x7E) {
    return NULL;
  }
  size_t offset = 0;
  for (char i = 0x20; i < c; i++) {
    offset += is_descender(i) ? 8 : 7;
  }
  return &glyph_bitmap[offset];
}

// The 5px glyph cell has real dead columns for narrow characters (e.g.
// '1' only lights columns 1-3, not 0-4) - drawing every glyph at a
// fixed advance left those built into the gap, so '1' visually had
// ~2px of whitespace on each side instead of the intended 1px
// (user-reported, real bug, not a rendering artifact). Scans the
// glyph's actual lit columns and returns its true content width plus
// where that content starts, so step_glyph() below can trim the dead
// columns instead of baking them into every character's spacing.
static int glyph_content_width(const uint8_t *glyph, int rows, int *left_out) {
  int left = 5, right = -1;
  for (int row = 0; row < rows; row++) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 5; col++) {
      if (bits & (0x80 >> col)) {
        if (col < left) left = col;
        if (col > right) right = col;
      }
    }
  }
  if (right < 0) { // blank glyph (space) - no lit pixels to measure
    *left_out = 0;
    return 3; // reasonable visual space width, roughly a narrow digit's content width
  }
  *left_out = left;
  return right - left + 1;
}

// Single sink for a lit glyph pixel - either the live matrix (dst_buf
// NULL, the original/only behavior before buffer-compositing existed)
// or a caller-owned CANVAS_WIDTH*CANVAS_HEIGHT*3 RGB888 buffer (see
// kaleidobox_font_composite_centered_to_buffer() below). Bounds-checked
// once here instead of at every call site.
static void plot(uint8_t *dst_buf, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (x < 0 || x >= 64 || y < 0 || y >= 64) {
    return;
  }
  if (dst_buf) {
    size_t idx = ((size_t)y * 64 + x) * 3;
    dst_buf[idx] = r;
    dst_buf[idx + 1] = g;
    dst_buf[idx + 2] = b;
  } else {
    kaleidobox_matrix_set_pixel((uint8_t)x, (uint8_t)y, r, g, b);
  }
}

// Single shared step used by drawing, width-measuring, AND halo mask-
// marking, so none of the three can ever disagree about a glyph's real
// position - advances *x by this glyph's real content width + 1px gap
// (both scaled by `scale`), and (when draw is true) draws it (trimmed
// to its real left edge, not the full 5px cell) and/or marks it into a
// mask buffer. scale=1 is a plain 1:1 pixel - each logical glyph pixel
// becomes an NxN block for scale>1, used to make the clock overlay's
// digits bigger than this font's native 7px height without a second
// font.
//
// mask (optional, NULL for the plain draw/measure cases) records which
// pixels are glyph-lit, relative to (mask_x0,mask_y0) with row stride
// mask_w - used by the halo-drawing paths below to dilate a border
// around the actual glyph strokes rather than a bounding rectangle.
//
// paint controls whether lit pixels actually get plotted, independent
// of mask-marking - the see-through mode below needs to walk/mark a
// glyph's real positions WITHOUT plotting anything for it (the glyph's
// interior must stay exactly whatever was already there, e.g. a live
// kaleidoscope frame). paint=false with dst_buf=NULL is that mode:
// plot(NULL, ...) would otherwise fall through to writing the LIVE
// MATRIX directly (that's what dst_buf=NULL means for every other
// caller here) - real bug avoided by gating the plot() call on `paint`
// explicitly rather than inferring "don't paint" from a NULL buffer.
static void step_glyph(int *x, char c, bool draw, int y, uint8_t *dst_buf,
                       uint8_t r, uint8_t g, uint8_t b, int scale, bool paint,
                       bool *mask, int mask_x0, int mask_y0, int mask_w,
                       int mask_h) {
  const uint8_t *glyph = glyph_ptr(c);
  if (!glyph) {
    *x += (3 + 1) * scale; // out-of-range char - treat like a blank/space
    return;
  }
  int rows = is_descender(c) ? 8 : 7; // descenders' tail is their 8th row
  int left;
  int width = glyph_content_width(glyph, rows, &left);
  if (draw) {
    for (int row = 0; row < rows; row++) {
      uint8_t bits = glyph[row];
      for (int col = left; col < left + width; col++) {
        if (bits & (0x80 >> col)) {
          int px0 = *x + (col - left) * scale;
          int py0 = y + row * scale;
          for (int dy = 0; dy < scale; dy++) {
            for (int dx = 0; dx < scale; dx++) {
              int px = px0 + dx, py = py0 + dy;
              if (mask) {
                int mx = px - mask_x0, my = py - mask_y0;
                if (mx >= 0 && mx < mask_w && my >= 0 && my < mask_h) {
                  mask[my * mask_w + mx] = true;
                }
              }
              if (paint) {
                plot(dst_buf, px, py, r, g, b);
              }
            }
          }
        }
      }
    }
  }
  *x += width * scale + scale;
}

static void draw_line(int x, int y, const char *text, uint8_t *dst_buf,
                      uint8_t r, uint8_t g, uint8_t b, int scale, bool *mask,
                      int mask_x0, int mask_y0, int mask_w, int mask_h) {
  for (const char *p = text; *p; p++) {
    step_glyph(&x, *p, true, y, dst_buf, r, g, b, scale, true, mask, mask_x0,
              mask_y0, mask_w, mask_h);
  }
}

// Walks the same real glyph positions as draw_line, marking `mask`
// without plotting anything anywhere - used by the see-through
// composite path, which needs to know exactly where the glyph strokes
// are without touching them.
static void mark_line(int x, int y, const char *text, int scale, bool *mask,
                      int mask_x0, int mask_y0, int mask_w, int mask_h) {
  for (const char *p = text; *p; p++) {
    step_glyph(&x, *p, true, y, NULL, 0, 0, 0, scale, false, mask, mask_x0,
              mask_y0, mask_w, mask_h);
  }
}

static int measure_line(const char *text, int scale) {
  int x = 0;
  for (const char *p = text; *p; p++) {
    step_glyph(&x, *p, false, 0, NULL, 0, 0, 0, scale, false, NULL, 0, 0, 0, 0);
  }
  return x > 0 ? x - scale : 0; // trim the last char's unused trailing gap
}

void kaleidobox_font_draw_text(int x, int y, const char *text, uint8_t r,
                               uint8_t g, uint8_t b) {
  draw_line(x, y, text, NULL, r, g, b, 1, NULL, 0, 0, 0, 0);
}

void kaleidobox_font_draw_text_centered(int y, const char *text, uint8_t r,
                                        uint8_t g, uint8_t b) {
  draw_line((64 - measure_line(text, 1)) / 2, y, text, NULL, r, g, b, 1, NULL,
            0, 0, 0, 0);
}

bool kaleidobox_font_draw_text_fit(int y, const char *text, uint8_t r,
                                   uint8_t g, uint8_t b) {
  int width = measure_line(text, 1);
  if (width > 64) {
    return false; // doesn't fit even with real (kerned) spacing - caller should fall back
  }
  draw_line((64 - width) / 2, y, text, NULL, r, g, b, 1, NULL, 0, 0, 0, 0);
  return true;
}

// Paints glyph pixels straight onto a caller-owned buffer, nothing
// else touched - no background fill, just the strokes. Used for the
// kaleidoscope clock overlay's "cutout" mode (digit-shaped holes
// punched straight into the live pattern, no plate behind them) - see
// kaleidobox_font_halo_centered_to_buffer() below for the mode that
// also pads a contrast border around each stroke. scale=1 is native
// 7px-tall size; 2 is the practical max for a 5-character "HH:MM"
// string on this 64px panel (3x clips - user-confirmed on hardware).
void kaleidobox_font_draw_text_centered_to_buffer(uint8_t *buf, int y,
                                                  const char *text, uint8_t r,
                                                  uint8_t g, uint8_t b,
                                                  int scale) {
  draw_line((64 - measure_line(text, scale)) / 2, y, text, buf, r, g, b, scale,
            NULL, 0, 0, 0, 0);
}

// Generous upper bound on the halo mask's size - covers "HH:MM" at
// scale=2 (the practical max, see above) plus a few pixels of halo on
// each side, with margin. A caller that somehow exceeds this just gets
// its halo silently clipped past the bound (same "degrade, don't
// crash" philosophy as plot()'s own panel-edge clipping) - not a
// concern for the two real callers (the clock overlay's solid and
// see-through modes).
#define HALO_MASK_W 64
#define HALO_MASK_H 20

// Shared by both halo-drawing modes below: paints bg_r/g/b at every
// pixel that ISN'T itself lit but IS within halo_px (Chebyshev/8-
// connected distance) of a lit one - a dilated border hugging the
// actual glyph strokes instead of a bounding rectangle. What happens
// to the lit pixels themselves is entirely up to the caller (already
// done, if at all, before this runs) - this only ever touches the
// border around them.
static void fill_halo(uint8_t *buf, const bool *lit, int mask_w, int mask_h,
                      int mask_x0, int mask_y0, int halo_px, uint8_t bg_r,
                      uint8_t bg_g, uint8_t bg_b) {
  for (int my = 0; my < mask_h; my++) {
    for (int mx = 0; mx < mask_w; mx++) {
      if (lit[my * mask_w + mx]) {
        continue;
      }
      bool near_lit = false;
      for (int dy = -halo_px; dy <= halo_px && !near_lit; dy++) {
        int ny = my + dy;
        if (ny < 0 || ny >= mask_h) {
          continue;
        }
        for (int dx = -halo_px; dx <= halo_px; dx++) {
          int nx = mx + dx;
          if (nx >= 0 && nx < mask_w && lit[ny * mask_w + nx]) {
            near_lit = true;
            break;
          }
        }
      }
      if (near_lit) {
        plot(buf, mask_x0 + mx, mask_y0 + my, bg_r, bg_g, bg_b);
      }
    }
  }
}

// Flood-fills every mask cell that ISN'T reachable from the mask's own
// border by walking cells NOT within halo_px (Chebyshev) of a lit
// pixel (4-connected) - i.e. a pocket that's fully surrounded, once
// dilated by the same halo_px used for the visible border, by a
// glyph's own strokes. This is morphological closing (dilate, then
// flood-fill what's now cut off) rather than a strict lit-only
// enclosure test - a strict test only caught fully-enclosed loops like
// "0"'s counter; concave-but-technically-open pockets (e.g. "2"'s
// notch between its top curve and diagonal stroke) are NOT enclosed by
// the raw strokes, only by the strokes' dilated halo, and were left
// showing whatever's underneath (confirmed on hardware via photo - a
// visibly separate defect from the between-character gap, even though
// both read as "a gap near this digit" from a glance). Two static
// arrays sized for the same HALO_MASK_W*HALO_MASK_H bound fill_halo()
// already uses, for the same reason (this runs on the kaleidoscope
// task's small FreeRTOS stack - a real stack this size would overflow
// it).
static void fill_enclosed(uint8_t *buf, const bool *lit, int mask_w,
                          int mask_h, int mask_x0, int mask_y0, int halo_px,
                          uint8_t bg_r, uint8_t bg_g, uint8_t bg_b) {
  static bool solid[HALO_MASK_W * HALO_MASK_H];
  static bool reachable[HALO_MASK_W * HALO_MASK_H];
  static int stack[HALO_MASK_W * HALO_MASK_H];
  memset(reachable, 0, sizeof(reachable));

  for (int my = 0; my < mask_h; my++) {
    for (int mx = 0; mx < mask_w; mx++) {
      int idx = my * mask_w + mx;
      if (lit[idx]) {
        solid[idx] = true;
        continue;
      }
      bool near_lit = false;
      for (int dy = -halo_px; dy <= halo_px && !near_lit; dy++) {
        int ny = my + dy;
        if (ny < 0 || ny >= mask_h) {
          continue;
        }
        for (int dx = -halo_px; dx <= halo_px; dx++) {
          int nx = mx + dx;
          if (nx >= 0 && nx < mask_w && lit[ny * mask_w + nx]) {
            near_lit = true;
            break;
          }
        }
      }
      solid[idx] = near_lit;
    }
  }

  int sp = 0;
#define PUSH_IF_OPEN(idx)                                                    \
  do {                                                                       \
    if (!solid[idx] && !reachable[idx]) {                                    \
      reachable[idx] = true;                                                 \
      stack[sp++] = idx;                                                     \
    }                                                                        \
  } while (0)

  for (int mx = 0; mx < mask_w; mx++) {
    PUSH_IF_OPEN(mx);
    PUSH_IF_OPEN((mask_h - 1) * mask_w + mx);
  }
  for (int my = 0; my < mask_h; my++) {
    PUSH_IF_OPEN(my * mask_w);
    PUSH_IF_OPEN(my * mask_w + (mask_w - 1));
  }

  while (sp > 0) {
    int idx = stack[--sp];
    int mx = idx % mask_w, my = idx / mask_w;
    if (mx > 0) PUSH_IF_OPEN(idx - 1);
    if (mx < mask_w - 1) PUSH_IF_OPEN(idx + 1);
    if (my > 0) PUSH_IF_OPEN(idx - mask_w);
    if (my < mask_h - 1) PUSH_IF_OPEN(idx + mask_w);
  }
#undef PUSH_IF_OPEN

  for (int my = 0; my < mask_h; my++) {
    for (int mx = 0; mx < mask_w; mx++) {
      int idx = my * mask_w + mx;
      if (!lit[idx] && !reachable[idx]) {
        plot(buf, mask_x0 + mx, mask_y0 + my, bg_r, bg_g, bg_b);
      }
    }
  }
}

// Paints bg_r/g/b into the gap between each pair of adjacent characters
// (the fixed `scale`-px advance step_glyph() puts after every glyph),
// spanning the full halo height (glyph height PLUS halo_px above and
// below, not just the glyph rows) - the halo border curves outward at
// each glyph's corners (Chebyshev dilation), so a gap-fill limited to
// just the glyph's own 7*scale rows left a diagonal sliver of live
// pattern exposed right where the flat gap strip met the halo's curved
// corner. Those gap columns sit between two separate glyphs, open to
// the mask's top/bottom border on both sides, so they're never
// "enclosed" the way a single glyph's own hole is - fill_enclosed()
// above correctly leaves them alone - and fill_halo()'s dilation only
// closes them by coincidence: a narrow glyph like "1" barely dilates
// past its own thin stroke, so a wide neighbor like "2" right next to
// it left a real notch of live pattern showing at the seam (confirmed
// on hardware - outline mode specifically, narrow-next-to-wide is
// exactly where the dilation alone falls short).
//
// Row-aware, not a blind rectangle: an earlier version filled every row
// of the full glyph+halo height unconditionally, which for a
// mostly-empty neighbor like ":" (ink only at 4 of its 14 scale-2 rows)
// produced a flat black pillar standing the full digit height between
// characters - visually a "weird artifact" on hardware, not the
// tapered halo look everywhere else. Fixed by only filling a gap cell
// when its OWN row has a lit pixel within `reach` columns on the
// mask - i.e. only where one of the two flanking glyphs actually has a
// stroke nearby at that height, so the fill tapers with the real
// glyph shapes instead of standing as a rigid bar. Used by both the
// outline and see-through modes below. Walks
// glyph_ptr()/glyph_content_width() itself (rather than reusing
// step_glyph()) since it only needs each character's advance, not any
// drawing/masking side effect.
static void fill_between_glyphs(uint8_t *buf, int x, int y, const char *text,
                                int scale, int halo_px, const bool *lit,
                                int mask_x0, int mask_y0, int mask_w,
                                int mask_h, uint8_t bg_r, uint8_t bg_g,
                                uint8_t bg_b) {
  const int reach = halo_px + 4;
  for (const char *p = text; *p; p++) {
    const uint8_t *glyph = glyph_ptr(*p);
    int rows = is_descender(*p) ? 8 : 7;
    int left, width = glyph ? glyph_content_width(glyph, rows, &left) : 3;
    x += width * scale;
    if (p[1]) {
      for (int gy = -halo_px; gy < 7 * scale + halo_px; gy++) {
        int py = y + gy;
        int my = py - mask_y0;
        if (my < 0 || my >= mask_h) {
          continue;
        }
        for (int gx = 0; gx < scale; gx++) {
          int px = x + gx;
          int mx = px - mask_x0;
          bool near_lit = false;
          for (int dx = -reach; dx <= reach && !near_lit; dx++) {
            int nx = mx + dx;
            if (nx >= 0 && nx < mask_w && lit[my * mask_w + nx]) {
              near_lit = true;
            }
          }
          if (near_lit) {
            plot(buf, px, py, bg_r, bg_g, bg_b);
          }
        }
      }
    }
    x += scale;
  }
}

// Paints the text in fg_r/g/b, same as kaleidobox_font_draw_text_centered_to_buffer(),
// plus a dilated border of bg_r/g/b hugging each glyph STROKE (not a
// bounding rectangle) - contrast right at each digit's edge without a
// big flat plate competing with whatever's behind it - bg_r/g/b
// flood-filled into any hole fully enclosed by a glyph's own strokes
// (e.g. "0"'s counter), which the halo's dilation alone doesn't reach -
// and bg_r/g/b filled between each pair of adjacent characters too
// (see fill_between_glyphs() above), since a narrow glyph like "1" next
// to a wide one like "2" left the halo dilation alone unable to bridge
// that seam. Used by the clock overlay's "outline" mode; an earlier
// version of this filled a full rectangular plate instead - dropped
// after the user found a hard rectangle around the digits visually
// competed with the kaleidoscope pattern more than this halo does (that
// plate option is back as its own separate "rectangle" mode, see
// kaleidobox_font_rect_centered_to_buffer() below).
void kaleidobox_font_outline_centered_to_buffer(
    uint8_t *buf, int y, const char *text, uint8_t bg_r, uint8_t bg_g,
    uint8_t bg_b, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b, int scale,
    int halo_px) {
  int width = measure_line(text, scale);
  int text_x = (64 - width) / 2;

  int mask_x0 = text_x - halo_px;
  int mask_y0 = y - halo_px;
  int mask_w = width + 2 * halo_px;
  int mask_h = 7 * scale + 2 * halo_px;
  if (mask_w > HALO_MASK_W) {
    mask_w = HALO_MASK_W;
  }
  if (mask_h > HALO_MASK_H) {
    mask_h = HALO_MASK_H;
  }

  static bool lit[HALO_MASK_W * HALO_MASK_H];
  memset(lit, 0, sizeof(lit));

  // Marks the mask AND paints fg in the same pass - both always want
  // to happen together for every lit pixel here, no need for two
  // separate walks.
  draw_line(text_x, y, text, buf, fg_r, fg_g, fg_b, scale, lit, mask_x0,
            mask_y0, mask_w, mask_h);
  fill_enclosed(buf, lit, mask_w, mask_h, mask_x0, mask_y0, halo_px, bg_r,
               bg_g, bg_b);
  fill_between_glyphs(buf, text_x, y, text, scale, halo_px, lit, mask_x0,
                      mask_y0, mask_w, mask_h, bg_r, bg_g, bg_b);
  fill_halo(buf, lit, mask_w, mask_h, mask_x0, mask_y0, halo_px, bg_r, bg_g,
           bg_b);
}

// Opposite of the outline mode above: leaves each glyph stroke
// completely untouched (whatever's already in buf keeps showing there -
// e.g. a live kaleidoscope frame, genuinely visible through the digit
// shape), but paints a black border around that shape's edge (any other
// halo color was reported unreadable against the moving pattern behind
// it - so unlike outline mode this doesn't take a caller color at all)
// sized halo_px=scale (a fixed 2px halo swallowed a 1x-scale glyph
// whole - at 7px tall a 2px border on every side is most of the
// glyph), plus fills the gap between adjacent characters black too
// (see fill_between_glyphs() above) so two digits side by side don't
// have the live pattern bleeding through the space between them, plus
// closes any concave pocket small enough to disappear under that same
// halo_px dilation (see fill_enclosed() above - e.g. "2"'s notch
// between its top curve and diagonal stroke, confirmed on hardware).
// Genuinely large open interiors, like "0"'s counter, are wider than
// the closing radius and stay transparent - the whole point of this
// mode. Used by the clock overlay's "see-through" mode. An earlier
// version of this had no border at all (just the bare transparent
// strokes) and was reported unreadable against a moving pattern - this
// is that same idea with the readability problems fixed, not the
// version that got pulled.
void kaleidobox_font_seethrough_centered_to_buffer(uint8_t *buf, int y,
                                                    const char *text,
                                                    int scale) {
  int halo_px = scale;
  int width = measure_line(text, scale);
  int text_x = (64 - width) / 2;

  int mask_x0 = text_x - halo_px;
  int mask_y0 = y - halo_px;
  int mask_w = width + 2 * halo_px;
  int mask_h = 7 * scale + 2 * halo_px;
  if (mask_w > HALO_MASK_W) {
    mask_w = HALO_MASK_W;
  }
  if (mask_h > HALO_MASK_H) {
    mask_h = HALO_MASK_H;
  }

  static bool lit[HALO_MASK_W * HALO_MASK_H];
  memset(lit, 0, sizeof(lit));

  mark_line(text_x, y, text, scale, lit, mask_x0, mask_y0, mask_w, mask_h);
  fill_enclosed(buf, lit, mask_w, mask_h, mask_x0, mask_y0, halo_px, 0, 0, 0);
  fill_between_glyphs(buf, text_x, y, text, scale, halo_px, lit, mask_x0,
                      mask_y0, mask_w, mask_h, 0, 0, 0);
  fill_halo(buf, lit, mask_w, mask_h, mask_x0, mask_y0, halo_px, 0, 0, 0);
}

// Fills a padded rectangle behind the text with bg_r/g/b (a flat plate,
// not a stroke-hugging halo), then paints the text in fg_r/g/b on top -
// simplest/most readable of the four modes at the cost of a hard-edged
// plate competing visually with the kaleidoscope pattern around it (see
// kaleidobox_font_outline_centered_to_buffer() above for the mode that
// avoids that by hugging the strokes instead). Used by the clock
// overlay's "rectangle" mode. Same 2px pad as the outline modes' default
// halo_px so all four modes read as one consistent design.
void kaleidobox_font_rect_centered_to_buffer(uint8_t *buf, int y,
                                             const char *text, uint8_t bg_r,
                                             uint8_t bg_g, uint8_t bg_b,
                                             uint8_t fg_r, uint8_t fg_g,
                                             uint8_t fg_b, int scale) {
  const int pad = 2;
  int width = measure_line(text, scale);
  int text_x = (64 - width) / 2;
  int rect_x0 = text_x - pad;
  int rect_y0 = y - pad;
  int rect_w = width + 2 * pad;
  int rect_h = 7 * scale + 2 * pad;

  for (int ry = 0; ry < rect_h; ry++) {
    for (int rx = 0; rx < rect_w; rx++) {
      plot(buf, rect_x0 + rx, rect_y0 + ry, bg_r, bg_g, bg_b);
    }
  }
  draw_line(text_x, y, text, buf, fg_r, fg_g, fg_b, scale, NULL, 0, 0, 0, 0);
}

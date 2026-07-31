#pragma once

#include <stdbool.h>
#include <stdint.h>

// Draws one line of ASCII text (0x20-0x7E) starting at top-left (x,y) in
// panel pixel coordinates, using a 5x7 bitmap font. Proportionally
// spaced: each glyph's real lit-pixel width (e.g. '1' is 3px, '0' is
// 5px) plus a fixed 1px gap - not a uniform per-character advance, so
// narrow characters don't carry extra dead space from the font's 5px
// cell. Characters outside the printable ASCII range are skipped.
// Pixels outside the 64x64 panel are silently clipped.
void kaleidobox_font_draw_text(int x, int y, const char *text, uint8_t r,
                               uint8_t g, uint8_t b);

// Same as kaleidobox_font_draw_text(), but horizontally centered on the
// 64px-wide panel based on the text's rendered width, instead of a
// caller-supplied x.
void kaleidobox_font_draw_text_centered(int y, const char *text, uint8_t r,
                                        uint8_t g, uint8_t b);

// Draws one centered line if it fits within 64px at its real
// (proportional) width, returning true. Returns false and draws
// nothing if it doesn't fit - caller should fall back to something
// else (e.g. splitting across multiple lines).
bool kaleidobox_font_draw_text_fit(int y, const char *text, uint8_t r,
                                   uint8_t g, uint8_t b);

// Draws one centered line onto a caller-owned
// CANVAS_WIDTH*CANVAS_HEIGHT*3 RGB888 buffer instead of the live matrix
// - used to overlay text onto a frame before it's pushed (e.g. the
// kaleidoscope clock overlay's "cutout" mode: digit-shaped holes
// painted straight into the live pattern, nothing else touched, no
// background plate). scale=1 is the font's native 7px-tall size; each
// logical glyph pixel becomes an NxN block for scale>1 (e.g. 2 doubles
// both dimensions). Content still centers correctly at any scale since
// the width measurement scales the same way the drawing does.
void kaleidobox_font_draw_text_centered_to_buffer(uint8_t *buf, int y,
                                                  const char *text, uint8_t r,
                                                  uint8_t g, uint8_t b,
                                                  int scale);

// Same idea, but also pads a dilated border of bg_r/g/b out to halo_px
// pixels (Chebyshev/8-connected distance) around each glyph STROKE, not
// a bounding rectangle - contrast right at each digit's edge without a
// flat plate competing visually with whatever's behind it (e.g. a live
// kaleidoscope frame). Any pixel enclosed by a glyph's own strokes on
// all sides (e.g. the hole in "0"/"8") gets flood-filled with bg_r/g/b
// too, instead of the halo's dilation - which only reaches halo_px from
// a stroke - leaving the live pattern showing through a digit's own
// counter. Used by the clock overlay's "outline" mode.
void kaleidobox_font_outline_centered_to_buffer(
    uint8_t *buf, int y, const char *text, uint8_t bg_r, uint8_t bg_g,
    uint8_t bg_b, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b, int scale,
    int halo_px);

// Opposite of the outline mode above: leaves each glyph stroke
// completely untouched (whatever's already in buf keeps showing through
// - e.g. a live kaleidoscope frame, genuinely visible inside the digit
// shape), but paints a black border around that shape's edge (any other
// halo color was reported unreadable against a moving pattern) sized
// halo_px=scale (a fixed 2px halo swallowed a 1x-scale glyph whole), and
// fills the gap between adjacent characters black too - those gaps
// aren't "enclosed" by any single glyph's strokes so the halo dilation
// alone never reached them, leaving distracting gaps of live pattern
// between digits. Used by the clock overlay's "see-through" mode.
void kaleidobox_font_seethrough_centered_to_buffer(uint8_t *buf, int y,
                                                    const char *text,
                                                    int scale);

// Fills a padded rectangle behind the text with bg_r/g/b (a flat plate,
// not a stroke-hugging halo) then paints the text in fg_r/g/b on top.
// Used by the clock overlay's "rectangle" mode - simplest/most readable
// option at the cost of the plate competing visually with the pattern
// around it (see kaleidobox_font_outline_centered_to_buffer() above for
// the alternative that avoids that).
void kaleidobox_font_rect_centered_to_buffer(uint8_t *buf, int y,
                                             const char *text, uint8_t bg_r,
                                             uint8_t bg_g, uint8_t bg_b,
                                             uint8_t fg_r, uint8_t fg_g,
                                             uint8_t fg_b, int scale);

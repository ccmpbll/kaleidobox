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

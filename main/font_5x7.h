#pragma once

#include <stdbool.h>
#include <stdint.h>

// Draws one line of ASCII text (0x20-0x7E) starting at top-left (x,y) in
// panel pixel coordinates, using a 5x7 bitmap font with 1px spacing (6px
// advance per character). Characters outside the printable ASCII range
// are skipped. Pixels outside the 64x64 panel are silently clipped.
void kaleidobox_font_draw_text(int x, int y, const char *text, uint8_t r,
                               uint8_t g, uint8_t b);

// Same as kaleidobox_font_draw_text(), but horizontally centered on the
// 64px-wide panel based on the text's rendered width, instead of a
// caller-supplied x.
void kaleidobox_font_draw_text_centered(int y, const char *text, uint8_t r,
                                        uint8_t g, uint8_t b);

// Draws one centered line, tightening letter-spacing (5px advance
// instead of 6px) if needed to fit the full 64px width - only falls
// back to the caller-visible "doesn't fit" case (returns false, draws
// nothing) if even that isn't enough room. Use this when you'd rather
// have one tightly-packed line than an always-split multi-line layout,
// e.g. an IP address short enough to fit on its own.
bool kaleidobox_font_draw_text_fit(int y, const char *text, uint8_t r,
                                   uint8_t g, uint8_t b);

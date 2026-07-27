#pragma once

#include <stdint.h>

// Draws one line of ASCII text (0x20-0x7E) starting at top-left (x,y) in
// panel pixel coordinates, using a 5x7 bitmap font with 1px spacing (6px
// advance per character). Characters outside the printable ASCII range
// are skipped. Pixels outside the 64x64 panel are silently clipped.
void kaleidobox_font_draw_text(int x, int y, const char *text, uint8_t r,
                               uint8_t g, uint8_t b);

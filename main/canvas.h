#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// The live draw-mode pixel buffer (64x64 RGB888). Mutated either from
// batched /ws/draw messages (instant-draw mode) or in one shot from a
// full-grid POST to /api/canvas/submit (draw-then-submit mode) - the
// client picks which by how it sends data, both paths already exist
// server-side. Each set_pixel() call also writes straight into the
// matrix driver (see matrix.h), live immediately - matrix.cpp runs
// single-buffered, so there's currently no separate "show it now" step.

#define CANVAS_WIDTH 64
#define CANVAS_HEIGHT 64

esp_err_t kaleidobox_canvas_init(void);
void kaleidobox_canvas_set_pixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g,
                                 uint8_t b);
const uint8_t *kaleidobox_canvas_buffer(void); // CANVAS_WIDTH*CANVAS_HEIGHT*3 bytes, RGB888

// Replaces the entire grid in one shot (CANVAS_WIDTH*CANVAS_HEIGHT*3
// bytes, RGB888) - used for draw-then-submit and clear. Pushes via
// matrix.h's bulk draw call, not CANVAS_WIDTH*CANVAS_HEIGHT individual
// set_pixel() calls - see matrix.h for why that mattered.
void kaleidobox_canvas_set_all(const uint8_t *rgb888);

// Same as set_all(), but blends from the current buffer to rgb888 over
// a short series of intermediate frames instead of jumping straight to
// the new image - for gallery image switches (see gallery.c), where an
// instant cut reads as a jarring flash. If kaleidoscope is running,
// each intermediate frame is fed in as its live source instead of
// drawn straight to the matrix (a direct write would just be stomped
// by kaleidoscope's own next animation frame - see kaleidoscope.c), so
// the fold pattern itself renders from a gradually-blending image.
void kaleidobox_canvas_set_all_crossfade(const uint8_t *rgb888);

// Re-pushes the canvas's existing content to the matrix (and resumes
// kaleidoscope from it) without marking it dirty - for callers
// restoring/repainting what's already there, not applying new content.
// See the implementation comment in canvas.c for why this exists
// instead of calling set_all(kaleidobox_canvas_buffer()).
void kaleidobox_canvas_repaint(void);

// Forwards to matrix.h's flip - currently a no-op (single-buffer mode,
// see matrix.cpp). Kept for kaleidoscope mode, which will re-enable
// double buffering and need this. Not called anywhere in draw mode.
void kaleidobox_canvas_flip(void);

// True if the buffer has changed since the last kaleidobox_canvas_clear_dirty()
// call. Consumed by gallery.c's background task to autosave the buffer
// to the TF card only when it's actually changed, so a reboot can
// restore whatever was last showing instead of coming back up blank.
// Deliberately split from clear_dirty() (not a combined test-and-clear)
// so the caller only clears it once the save actually succeeds - a
// failed write (e.g. a transient SD DMA allocation failure) should
// retry next tick, not silently drop the pending save.
bool kaleidobox_canvas_is_dirty(void);
void kaleidobox_canvas_clear_dirty(void);

// Explicit "the user is actively drawing right now" signal - separate
// from set_pixel/set_all above, which are also called programmatically
// (gallery load, kaleidoscope restore, panel takeovers restoring prior
// content) and must NOT count as draw activity. Callers mark activity
// only from the two real user-draw entry points in http_server.c
// (ws_draw_handler, canvas_submit_post_handler). Consumed by
// panel_takeover.c to avoid yanking the panel away from someone
// mid-draw for a PrintSpy/weather takeover.
void kaleidobox_canvas_mark_draw_activity(void);
int64_t kaleidobox_canvas_ms_since_draw_activity(void);

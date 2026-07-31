#include "panel_takeover.h"

#include "canvas.h"
#include "kaleidoscope.h"
#include "settings.h"

// If the user drew a pixel more recently than this, don't yank the
// panel away from them for a takeover - same "explicit user action
// wins" reasoning as Clear-stops-kaleidoscope, just applied before the
// fact instead of after.
#define DRAW_ACTIVITY_GUARD_MS 5000

static bool g_active = false;
static bool g_was_kaleido_running = false;
static bool g_was_gallery_auto = false;

bool kaleidobox_panel_takeover_active(void) { return g_active; }

bool kaleidobox_panel_takeover_begin(void) {
  if (g_active) {
    return false;
  }
  if (kaleidobox_canvas_ms_since_draw_activity() < DRAW_ACTIVITY_GUARD_MS) {
    return false;
  }

  // NVS flag, not the live task state - kaleidoscope's own boot-resume
  // is deferred ~10s (see wifi.c's ip_display_timeout_cb), so a
  // takeover that begins in that window would otherwise see
  // is_running()==false and wrongly conclude kaleidoscope wasn't
  // supposed to be running at all, permanently losing the resume once
  // this takeover ends. The NVS flag reflects durable intent (what it
  // was doing before the last stop/reboot) regardless of whether the
  // live task has actually gotten around to starting yet.
  g_was_kaleido_running = kaleidobox_nvs_get_kaleido_running();
  g_was_gallery_auto = kaleidobox_nvs_get_gallery_auto_advance();

  // Transient variant, NOT kaleidobox_kaleidoscope_stop() - this pause
  // is internal/temporary (display rotation cycling to a printer/
  // weather slot), not the user asking to stop, and must not persist
  // kaleido_running=false to NVS. We already snapshotted the real
  // resume-intent into g_was_kaleido_running above; the stop() variant
  // would additionally overwrite that same NVS flag as an unwanted side
  // effect - with rotation cycling every ~15-30s, a reflash landing
  // mid-takeover was persisting "not running" even though the user's
  // actual last action was to leave it on. No-op if not running;
  // restores canvas->matrix as a side effect (see kaleidoscope.c) so
  // the takeover starts from a clean base even before the caller draws
  // its own content over it.
  kaleidobox_kaleidoscope_stop_transient();
  if (g_was_gallery_auto) {
    kaleidobox_nvs_set_gallery_auto_advance(false);
  }

  g_active = true;
  return true;
}

void kaleidobox_panel_takeover_end(void) {
  if (!g_active) {
    return;
  }

  // Same restore idiom as wifi.c's ip_display_timeout_cb: repaint
  // whatever canvas.c's buffer holds, then conditionally resume
  // kaleidoscope against that same buffer.
  kaleidobox_canvas_set_all(kaleidobox_canvas_buffer());

  if (g_was_gallery_auto) {
    kaleidobox_nvs_set_gallery_auto_advance(true);
  }
  if (g_was_kaleido_running) {
    kaleidobox_image_t source = {
        .rgb888 = (uint8_t *)kaleidobox_canvas_buffer(),
        .width = CANVAS_WIDTH,
        .height = CANVAS_HEIGHT,
    };
    kaleidobox_kaleidoscope_start(&source);
  }

  g_active = false;
}

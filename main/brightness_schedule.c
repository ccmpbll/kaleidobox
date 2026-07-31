#include "brightness_schedule.h"

#include "matrix.h"
#include "settings.h"
#include <stdbool.h>
#include <time.h>

// -1 (not yet known) rather than false, so the very first tick after
// boot always treats it as a transition if we're already inside the dim
// window (e.g. device rebooted at 2am with a 22:00-07:00 schedule) -
// otherwise it would stay at the old (bright) brightness until the next
// real crossing, potentially most of a day away.
static int g_was_dim = -1;

void kaleidobox_brightness_schedule_tick(void) {
  if (!kaleidobox_nvs_get_brightness_schedule_enabled()) {
    g_was_dim = -1; // re-arm the boot-edge case above if re-enabled later
    return;
  }

  time_t now = time(NULL);
  struct tm tm_now;
  localtime_r(&now, &tm_now);
  int minutes_now = tm_now.tm_hour * 60 + tm_now.tm_min;
  int dim_minutes = kaleidobox_nvs_get_dim_hour() * 60 + kaleidobox_nvs_get_dim_min();
  int bright_minutes = kaleidobox_nvs_get_bright_hour() * 60 + kaleidobox_nvs_get_bright_min();

  // Window can wrap past midnight (e.g. dim=22:00, bright=07:00) or not
  // (e.g. dim=07:00, bright=22:00) - both are valid schedules, handled
  // the same way either direction.
  bool should_be_dim;
  if (dim_minutes <= bright_minutes) {
    should_be_dim = minutes_now >= dim_minutes && minutes_now < bright_minutes;
  } else {
    should_be_dim = minutes_now >= dim_minutes || minutes_now < bright_minutes;
  }

  if (should_be_dim == (g_was_dim == 1)) {
    return; // no edge - a manual mid-window change (if any) stays untouched
  }
  g_was_dim = should_be_dim ? 1 : 0;

  // Deliberately does NOT call kaleidobox_nvs_set_brightness() on the
  // dim edge - the persisted `brightness` setting is the user's own
  // manual/bright value and must stay untouched, or the bright edge
  // below would have nothing real left to restore to (there's no
  // separate "bright_val" setting - see settings.h). Only the live
  // matrix brightness changes during the dim window; a reboot mid-dim
  // briefly shows the bright value again until this tick next runs
  // (within ~1s, see gallery_bg_task's cadence) and self-corrects -
  // an acceptable cosmetic blip, not worth persisting extra state for.
  if (should_be_dim) {
    kaleidobox_matrix_set_brightness(kaleidobox_nvs_get_dim_brightness());
  } else {
    kaleidobox_matrix_set_brightness(kaleidobox_nvs_get_brightness());
  }
}

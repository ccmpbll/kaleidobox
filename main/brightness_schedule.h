#pragma once

// Dims the panel at dim_hour:dim_min, restores it at
// bright_hour:bright_min (see main/settings.h's bright_sched_en/dim_*/
// bright_* settings) - edge-triggered, only acts exactly at each
// crossing, so a manual brightness change mid-window sticks until the
// next real crossing instead of being silently overwritten. Call once
// per tick from an already-running periodic task - see gallery.c's
// gallery_bg_task, which already runs a continuous 1s tick regardless
// of what mode is active; a once-a-minute concept doesn't need its own
// dedicated task when checking two ints every second is free.
void kaleidobox_brightness_schedule_tick(void);

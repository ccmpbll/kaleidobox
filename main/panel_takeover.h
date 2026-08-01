#pragma once

#include <stdbool.h>

// Shared "stop whatever's showing, do something else, restore it
// later" primitive for panel-takeover features (PrintSpy print-status,
// weather) - this pattern existed twice already, ad-hoc, before these
// two consumers (wifi.c's boot-time IP display, and client-side JS in
// web/app.html's Clear button) - not reused directly here since neither
// prior instance is a standalone C function, but the underlying
// snapshot/restore idiom (kaleidobox_canvas_set_all() to restore,
// conditionally restart kaleidoscope) is copied from wifi.c's
// ip_display_timeout_cb exactly.
//
// Single static "active" flag gives free mutual exclusion between the
// two real consumers - PrintSpy takes priority in practice since it's
// event-driven (starts the moment a print begins), weather is
// timer-driven and just checks active() before starting, re-checking
// next tick if something else already has the panel. No takeover-reason
// enum, no priority system, no nesting - there are exactly 2 consumers
// and only one can ever run.

bool kaleidobox_panel_takeover_active(void);

// Returns false (does nothing) if a takeover is already active, or if
// there's been draw activity within the last few seconds - callers
// should just skip this cycle and try again later rather than treating
// false as an error. On success, stops kaleidoscope/gallery-auto-advance
// (snapshotting whether they were running) and marks active.
bool kaleidobox_panel_takeover_begin(void);

// Restores the canvas to the live matrix, resumes kaleidoscope/gallery-
// auto-advance if they were active before begin(), clears active.
// No-op if not currently active.
void kaleidobox_panel_takeover_end(void);

// Same immediate teardown as end() (repaints the canvas, clears
// active), but deliberately does NOT restore gallery-auto-advance or
// resume kaleidoscope - for callers that want everything left off
// rather than restored (display_rotation.c's pause, for uninterrupted
// drawing - the point is nothing takes the panel back, not "put it
// back the way it was"). No-op if not currently active.
void kaleidobox_panel_takeover_cancel(void);

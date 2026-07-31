#pragma once

// PrintSpy print-status tracking - subscribes to printspy's MQTT
// publish topic (see main/settings.h's printspy_* settings; default
// wildcard "printspy/printer/+/state", one retained message per
// printer, published on state transitions and every poll tick while
// that printer is actively printing). Tracks every printer's
// last-known state in a small fixed table. Does not decide when to
// show it on the panel - see main/display_rotation.c, which polls
// kaleidobox_printspy_printing_count()/_render_printing() below.
//
// Idempotent, safe to call again on a WiFi reconnect - mirrors
// start_mdns()/kaleidobox_clock_start_sntp()'s own idempotency in
// wifi.c. No-ops entirely if printspy_en is off or no broker is
// configured.
void kaleidobox_printspy_start(void);

// Live disconnect - see the implementation comment in printspy.c. Safe
// to call even if not currently connected (no-op).
void kaleidobox_printspy_stop(void);

// Number of tracked printers currently printing and not yet stale (no
// fresh message in the last ~120s).
int kaleidobox_printspy_printing_count(void);

// Renders the idx'th currently-printing printer (0-based, same
// enumeration order as kaleidobox_printspy_printing_count()) directly
// to the matrix. No-op if idx is out of range.
void kaleidobox_printspy_render_printing(int idx);

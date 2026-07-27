#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// Hooks esp_log_set_vprintf() to also capture every ESP_LOG* line into an
// in-memory ring buffer, so the web app can show a live log console
// without a serial connection. Still calls through to the original
// vprintf - serial output is unaffected. Ported unchanged from
// printspy-cam.
esp_err_t kaleidobox_log_init(void);

// Reads log lines newer than *cursor (0 to start from the oldest line
// still buffered) into buf as newline-separated, null-terminated text.
// Advances *cursor past what was read. Returns bytes written (0 if
// nothing new fit or was available). If the reader falls far enough
// behind that its cursor points at an already-evicted line, this silently
// jumps forward to the oldest still-available line - no durability
// guarantee, this is a live console, not a log store.
size_t kaleidobox_log_read(uint64_t *cursor, char *buf, size_t buf_size);

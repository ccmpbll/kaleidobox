#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

// Serves the web app + API (see README.md for the full endpoint table).
// Idempotent - safe to call again on WiFi reconnect (only starts the
// server once, same as printspy-cam's http_server.h).
//
// /api/logs and /ws/draw each need to stay responsive while other
// requests are in flight; /api/logs reuses printspy-cam's async-worker-
// pool pattern (blocking SSE loop moved off the shared httpd task) since
// esp_http_server otherwise services all connections from one task by
// default.
esp_err_t kaleidobox_http_server_start(void);

#include "http_server.h"

#include "canvas.h"
#include "cJSON.h"
#include "clock.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gallery.h"
#include "image_decode.h"
#include "kaleidoscope.h"
#include "log.h"
#include "matrix.h"
#include "ota.h"
#include "printspy.h"
#include "sdcard.h"
#include "settings.h"
#include "version.h"
#include "weather.h"
#include "wifi.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "kaleidobox_http";

// req->uri is never URL-decoded by esp_http_server - a gallery name
// with a space or other reserved character (routine for real photo
// filenames, and what the web UI's nameFromFilename() produces)
// arrives here as literal percent-encoding (e.g. "my%20photo") unless
// decoded first. Real bug, not hypothetical: caught when a name with a
// space got saved to disk as literal "my%20photo.raw". Copies (not
// decodes in place) since req->uri's backing array is declared const,
// and the decoded string is never longer than the encoded one so dst
// only needs to be at least as large as src.
static void url_decode_uri_tail(const char *src, char *dst, size_t dst_size) {
  size_t out = 0;
  while (*src && out + 1 < dst_size) {
    if (src[0] == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
      char hex[3] = {src[1], src[2], 0};
      dst[out++] = (char)strtol(hex, NULL, 16);
      src += 3;
    } else {
      dst[out++] = *src++;
    }
  }
  dst[out] = '\0';
}

static httpd_handle_t server = NULL;

extern const uint8_t app_html_start[] asm("_binary_app_html_start");
extern const uint8_t app_html_end[] asm("_binary_app_html_end");
extern const uint8_t settings_html_start[] asm("_binary_settings_html_start");
extern const uint8_t settings_html_end[] asm("_binary_settings_html_end");
extern const uint8_t kaleidobox_png_start[] asm("_binary_kaleidobox_png_start");
extern const uint8_t kaleidobox_png_end[] asm("_binary_kaleidobox_png_end");
extern const uint8_t icon_512_png_start[] asm("_binary_icon_512_png_start");
extern const uint8_t icon_512_png_end[] asm("_binary_icon_512_png_end");

// Long-running handlers (SSE log console) block whichever task runs them
// until the client disconnects. esp_http_server services all connections
// from a single task by default, so without this, one open /api/logs
// would stall every other request until it closed. Only one such
// consumer exists (/api/logs), so this is a single dedicated worker
// task rather than a generic N-worker pool - see
// examples/protocols/http_server/async_handlers in esp-idf for
// Espressif's own multi-worker version of this pattern.
typedef struct {
  httpd_req_t *req;
  esp_err_t (*handler)(httpd_req_t *req);
} async_job_t;

static QueueHandle_t log_worker_queue;
static SemaphoreHandle_t log_worker_free;

static void log_worker_task(void *arg) {
  (void)arg;
  while (true) {
    xSemaphoreGive(log_worker_free);
    async_job_t job;
    if (xQueueReceive(log_worker_queue, &job, portMAX_DELAY)) {
      job.handler(job.req);
      httpd_req_async_handler_complete(job.req);
    }
  }
}

static esp_err_t log_worker_init(void) {
  log_worker_free = xSemaphoreCreateBinary();
  log_worker_queue = xQueueCreate(1, sizeof(async_job_t));
  if (!log_worker_free || !log_worker_queue) {
    return ESP_ERR_NO_MEM;
  }
  // Pinned to core 0 with the main httpd task, not left unpinned - same
  // networking-work-stays-off-core-1 reasoning (see
  // kaleidobox_http_server_start's config.core_id comment). This task
  // streams the live log console over the network same as any other
  // handler; no reason for it to risk landing on the core kaleidoscope/
  // gallery/rotation are pinned to.
  if (xTaskCreatePinnedToCore(log_worker_task, "log_worker", 4096, NULL,
                              tskIDLE_PRIORITY + 1, NULL, 0) != pdPASS) {
    ESP_LOGE(TAG, "xTaskCreate(log_worker) failed");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

static esp_err_t log_worker_dispatch(httpd_req_t *req,
                                     esp_err_t (*handler)(httpd_req_t *)) {
  httpd_req_t *copy = NULL;
  esp_err_t err = httpd_req_async_handler_begin(req, &copy);
  if (err != ESP_OK) {
    return err;
  }

  if (xSemaphoreTake(log_worker_free, 0) != pdTRUE) {
    httpd_req_async_handler_complete(copy);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, "Too many concurrent connections");
    return ESP_OK;
  }

  async_job_t job = {.req = copy, .handler = handler};
  if (xQueueSend(log_worker_queue, &job, 0) != pdTRUE) {
    httpd_req_async_handler_complete(copy);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, "Too many concurrent connections");
    return ESP_OK;
  }
  return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)app_html_start,
                         HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_page_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)settings_html_start,
                         HTTPD_RESP_USE_STRLEN);
}

// EMBED_FILES (unlike EMBED_TXTFILES) doesn't null-terminate the blob -
// it's arbitrary binary (PNG) data that can legitimately contain zero
// bytes, so length has to come from the linker-provided end pointer,
// not strlen.
static esp_err_t logo_png_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "image/png");
  // Embedded in the firmware image - only changes on reflash, so a long
  // cache lifetime is safe. Without this, every page load re-fetched it
  // uncached (twice - favicon link + header <img> both hit this same
  // URL), and a handful of these piling up concurrently was enough to
  // exhaust httpd's small socket pool and stall unrelated requests for
  // 30s+ (confirmed live - see max_open_sockets below).
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
  return httpd_resp_send(req, (const char *)kaleidobox_png_start,
                         kaleidobox_png_end - kaleidobox_png_start);
}

// Dedicated square, opaque-background version of the logo for
// apple-touch-icon/manifest use - iOS stretches a non-square icon to
// fit its own square mask instead of letterboxing it (confirmed on
// hardware: the hexagon logo came out visibly squished on a home
// screen), and separately fills transparent areas with black/white of
// its own choosing rather than respecting alpha. Padded onto a plain
// rgb(18,18,20) square (matching the page's own dark background) once,
// ahead of time, rather than trying to fight either behavior at
// runtime.
static esp_err_t icon_512_png_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "image/png");
  // Same reasoning as logo_png_handler above - embedded, build-fixed
  // content, safe to cache.
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
  return httpd_resp_send(req, (const char *)icon_512_png_start,
                         icon_512_png_end - icon_512_png_start);
}

static esp_err_t status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "version", KALEIDOBOX_VERSION);
  cJSON_AddNumberToObject(root, "uptime_seconds", esp_timer_get_time() / 1000000);

  esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip_info;
  if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    cJSON_AddStringToObject(root, "ip", ip_str);
  }

  wifi_config_t wifi_cfg = {0};
  if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK) {
    cJSON_AddStringToObject(root, "wifi_ssid", (const char *)wifi_cfg.sta.ssid);
  }

  cJSON_AddBoolToObject(root, "kaleidoscope_running",
                        kaleidobox_kaleidoscope_is_running());

  // Real monitoring value, not just a diagnostic throwaway - lets a
  // slow leak (or fragmentation) get caught by watching this trend
  // over time instead of only noticing once the device gets visibly
  // sluggish or crashes.
  cJSON_AddNumberToObject(root, "heap_free_bytes", esp_get_free_heap_size());
  cJSON_AddNumberToObject(root, "heap_min_free_bytes", esp_get_minimum_free_heap_size());
  // A flat free-byte total can still hide a badly fragmented heap
  // (plenty of free bytes, but no single block big enough for the next
  // 12KB canvas-sized allocation) - this is the number that actually
  // answers that.
  cJSON_AddNumberToObject(root, "heap_largest_free_block",
                          heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

// Polls the ring buffer every 300ms and pushes new lines to the browser as
// Server-Sent Events. Runs on an async worker (see above). Ported
// unchanged from printspy-cam.
static esp_err_t logs_async_handler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, "text/event-stream");
  if (res != ESP_OK) {
    return res;
  }
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  uint64_t cursor = 0;
  char buf[1024];
  while (true) {
    size_t len = kaleidobox_log_read(&cursor, buf, sizeof(buf));
    if (len > 0) {
      char *line = buf;
      while (line < buf + len) {
        char *nl = strchr(line, '\n');
        if (!nl) {
          break;
        }
        *nl = '\0';
        char frame[192];
        int flen = snprintf(frame, sizeof(frame), "data: %s\n\n", line);
        res = httpd_resp_send_chunk(req, frame, flen);
        if (res != ESP_OK) {
          return res;
        }
        line = nl + 1;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

static esp_err_t logs_handler(httpd_req_t *req) {
  return log_worker_dispatch(req, logs_async_handler);
}

// Reads the full request body into a heap buffer. Caller must free().
// Only used for small JSON bodies - not suitable for the multi-hundred-KB
// OTA/image uploads, which stream instead. Ported from printspy-cam.
static esp_err_t read_body(httpd_req_t *req, char **out) {
  if (req->content_len == 0 || req->content_len > 4096) {
    return ESP_ERR_INVALID_SIZE;
  }
  char *buf = malloc(req->content_len + 1);
  if (!buf) {
    return ESP_ERR_NO_MEM;
  }
  size_t received = 0;
  while (received < req->content_len) {
    int r = httpd_req_recv(req, buf + received, req->content_len - received);
    if (r <= 0) {
      free(buf);
      return ESP_FAIL;
    }
    received += r;
  }
  buf[received] = '\0';
  *out = buf;
  return ESP_OK;
}

static esp_err_t wifi_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
  cJSON *password_item = cJSON_GetObjectItem(json, "password");
  if (!cJSON_IsString(ssid_item) || strlen(ssid_item->valuestring) == 0) {
    cJSON_Delete(json);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Missing SSID");
    return ESP_FAIL;
  }

  wifi_config_t wifi_cfg = {0};
  strncpy((char *)wifi_cfg.sta.ssid, ssid_item->valuestring,
          sizeof(wifi_cfg.sta.ssid) - 1);
  if (cJSON_IsString(password_item)) {
    strncpy((char *)wifi_cfg.sta.password, password_item->valuestring,
            sizeof(wifi_cfg.sta.password) - 1);
  }
  cJSON_Delete(json);

  // esp_wifi_set_config persists to NVS flash storage on its own.
  esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");

  ESP_LOGI(TAG, "New WiFi credentials saved via web UI, restarting");
  vTaskDelay(pdMS_TO_TICKS(500)); // let the response flush
  // Reboot (rather than hot-swapping the STA config) so the fresh boot
  // gets the finite-retry-then-AP-fallback safety net for whatever
  // credentials were just entered, same as the AP setup flow.
  esp_restart();
  return ESP_OK; // unreachable
}

// Firmware image streamed straight through to the OTA partition as it
// arrives - not buffered in RAM. Ported unchanged from printspy-cam.
static esp_err_t ota_post_handler(httpd_req_t *req) {
  if (req->content_len == 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Missing body");
    return ESP_FAIL;
  }

  if (kaleidobox_ota_begin() != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  char buf[4096];
  size_t remaining = req->content_len;
  while (remaining > 0) {
    size_t to_read = remaining < sizeof(buf) ? remaining : sizeof(buf);
    int received = httpd_req_recv(req, buf, to_read);
    if (received <= 0) {
      kaleidobox_ota_abort();
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (kaleidobox_ota_write_chunk((uint8_t *)buf, received) != ESP_OK) {
      kaleidobox_ota_abort();
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_sendstr(req, "Firmware write failed");
      return ESP_FAIL;
    }
    remaining -= received;
  }

  if (kaleidobox_ota_finish() != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Firmware image invalid");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");

  ESP_LOGI(TAG, "OTA update applied via web UI, restarting");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK; // unreachable
}

// --- Draw mode ---------------------------------------------------------

// One WS text frame per pointermove batch: {"pixels":[{"x":n,"y":n,"r":n,
// "g":n,"b":n}, ...]}. The client interpolates and batches a whole
// stroke segment (since consecutive draw one at a time, not per-message)
// - originally one message per pixel, but that both dropped pixels
// during fast drags (no interpolation between sparse pointermove
// samples) and added visible drag lag (one WS round trip per pixel).
// Heap-allocated receive buffer, not a fixed stack one - a batch can be
// several KB, and this handler's task stack is only 8192 bytes total.
//
// Unlike /api/logs, a WS handler doesn't need the async pool - httpd
// invokes it per-frame as data arrives on the socket rather than once
// with a handler that blocks for the connection's whole lifetime.
static esp_err_t ws_draw_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    // WS handshake - nothing else to do, esp_http_server handles it.
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt = {0};
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK || ws_pkt.len == 0 || ws_pkt.len > 16384) {
    return ret;
  }

  char *buf = malloc(ws_pkt.len + 1);
  if (!buf) {
    return ESP_ERR_NO_MEM;
  }
  ws_pkt.payload = (uint8_t *)buf;
  ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
  if (ret != ESP_OK) {
    free(buf);
    return ret;
  }
  buf[ws_pkt.len] = '\0';

  cJSON *json = cJSON_Parse(buf);
  free(buf);
  if (!json) {
    return ESP_OK; // drop malformed frame, keep the connection alive
  }

  cJSON *pixels = cJSON_GetObjectItem(json, "pixels");
  if (cJSON_IsArray(pixels)) {
    cJSON *px = NULL;
    cJSON_ArrayForEach(px, pixels) {
      cJSON *x = cJSON_GetObjectItem(px, "x");
      cJSON *y = cJSON_GetObjectItem(px, "y");
      cJSON *r = cJSON_GetObjectItem(px, "r");
      cJSON *g = cJSON_GetObjectItem(px, "g");
      cJSON *b = cJSON_GetObjectItem(px, "b");
      if (cJSON_IsNumber(x) && cJSON_IsNumber(y) && cJSON_IsNumber(r) &&
          cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
        kaleidobox_canvas_set_pixel((uint8_t)x->valueint, (uint8_t)y->valueint,
                                   (uint8_t)r->valueint, (uint8_t)g->valueint,
                                   (uint8_t)b->valueint);
      }
    }
    // No kaleidobox_canvas_flip() here - single-buffer mode (see
    // matrix.cpp) means set_pixel() is already live, and flip() would
    // just be a no-op warning log per batch.

    // set_pixel() (unlike set_all()) doesn't go through canvas.c's own
    // kaleidoscope-update hook, since triggering that per-pixel inside
    // a tight batch loop would be wasteful - once per batch here
    // instead. No-op if kaleidoscope isn't running.
    if (kaleidobox_kaleidoscope_is_running()) {
      kaleidobox_image_t source = {
          .rgb888 = (uint8_t *)kaleidobox_canvas_buffer(),
          .width = CANVAS_WIDTH,
          .height = CANVAS_HEIGHT,
      };
      kaleidobox_kaleidoscope_update_source(&source);
    }
    kaleidobox_canvas_mark_draw_activity();
  }
  cJSON_Delete(json);
  return ESP_OK;
}

// Draw-then-submit mode: raw CANVAS_WIDTH*CANVAS_HEIGHT*3 RGB888 body,
// one shot.
// Raw RGB888 readback of the live canvas buffer - lets the web UI
// repaint itself on page load instead of showing a blank grid that
// doesn't match what's actually on the panel (draw and gallery-show
// both mutate this same buffer via kaleidobox_canvas_set_all/set_pixel,
// so this one endpoint covers both sources).
static esp_err_t canvas_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/octet-stream");
  return httpd_resp_send(req, (const char *)kaleidobox_canvas_buffer(),
                          CANVAS_WIDTH * CANVAS_HEIGHT * 3);
}

static esp_err_t canvas_submit_post_handler(httpd_req_t *req) {
  const size_t expected = CANVAS_WIDTH * CANVAS_HEIGHT * 3;
  if (req->content_len != expected) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Body must be CANVAS_WIDTH*CANVAS_HEIGHT*3 bytes");
    return ESP_FAIL;
  }

  // Heap-allocated, not a stack array - 12288 bytes doesn't fit this
  // handler's httpd task stack (8192 bytes total, see
  // kaleidobox_http_server_start's config.stack_size). A stack buffer
  // this size was silently corrupting the task stack on every
  // submit/clear - real bug, not hypothetical, matches the "breaks
  // until refresh" symptom. MALLOC_CAP_SPIRAM, not plain malloc() - same
  // internal-RAM-starvation class already fixed elsewhere (12KB is
  // under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL's threshold), and this
  // handler runs on every draw-then-submit and every Clear press.
  uint8_t *buf = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM);
  if (!buf) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  size_t received = 0;
  while (received < expected) {
    int r = httpd_req_recv(req, (char *)buf + received, expected - received);
    if (r <= 0) {
      free(buf);
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    received += r;
  }

  // One bulk driver call, not CANVAS_WIDTH*CANVAS_HEIGHT individual
  // set_pixel() calls - see matrix.h/canvas.h for why that mattered
  // (racing the live DMA scan, visible as bright flickering pixels).
  kaleidobox_canvas_set_all(buf);
  free(buf);
  kaleidobox_canvas_mark_draw_activity();

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// --- Upload --------------------------------------------------------------

// 8MB cap - generous for a phone photo (even at "original" quality) while
// keeping worst-case upload buffer + decode buffer comfortably inside
// 16MB PSRAM. Rejected outright at the content_len check below, before
// any allocation.
#define MAX_UPLOAD_BYTES (8 * 1024 * 1024)

// --- Kaleidoscope settings ------------------------------------------------

static esp_err_t kaleidoscope_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "fold_count", kaleidobox_nvs_get_fold_count());
  cJSON_AddBoolToObject(root, "motion_zoom", kaleidobox_nvs_get_motion_zoom());
  cJSON_AddBoolToObject(root, "running", kaleidobox_kaleidoscope_is_running());

  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

static esp_err_t kaleidoscope_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *item = cJSON_GetObjectItem(json, "fold_count");
  if (cJSON_IsNumber(item) && item->valueint >= 2 && item->valueint <= 32) {
    kaleidobox_nvs_set_fold_count((uint8_t)item->valueint);
  }
  item = cJSON_GetObjectItem(json, "motion_zoom");
  if (cJSON_IsBool(item)) {
    kaleidobox_nvs_set_motion_zoom(cJSON_IsTrue(item));
  }
  item = cJSON_GetObjectItem(json, "enabled");
  bool stop_requested = cJSON_IsBool(item) && !cJSON_IsTrue(item);
  bool start_requested = cJSON_IsBool(item) && cJSON_IsTrue(item);
  cJSON_Delete(json);

  if (stop_requested) {
    kaleidobox_kaleidoscope_stop();
  } else if (start_requested) {
    kaleidobox_image_t source = {
        .rgb888 = (uint8_t *)kaleidobox_canvas_buffer(),
        .width = CANVAS_WIDTH,
        .height = CANVAS_HEIGHT,
    };
    // Not yet implemented (see kaleidoscope.c) - reports the real state
    // back below rather than pretending it started.
    kaleidobox_kaleidoscope_start(&source);
  }

  httpd_resp_set_type(req, "application/json");
  char resp[64];
  snprintf(resp, sizeof(resp), "{\"ok\":true,\"running\":%s}",
           kaleidobox_kaleidoscope_is_running() ? "true" : "false");
  httpd_resp_sendstr(req, resp);
  return ESP_OK;
}

// --- Clock overlay (kaleidoscope mode only) ---------------------------------

static esp_err_t clock_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  uint8_t mode = kaleidobox_nvs_get_clock_mode();
  const char *mode_str = "off";
  if (mode == 1) {
    mode_str = "outline";
  } else if (mode == 2) {
    mode_str = "default";
  } else if (mode == 3) {
    mode_str = "seethrough";
  } else if (mode == 4) {
    mode_str = "rectangle";
  }
  cJSON_AddStringToObject(root, "mode", mode_str);

  char color_str[8];
  snprintf(color_str, sizeof(color_str), "#%06lx",
           (unsigned long)kaleidobox_nvs_get_clock_color());
  cJSON_AddStringToObject(root, "color", color_str);
  cJSON_AddNumberToObject(root, "scale", kaleidobox_nvs_get_clock_scale());
  cJSON_AddBoolToObject(root, "24h", kaleidobox_nvs_get_clock_24h());
  cJSON_AddStringToObject(root, "ntp_server", kaleidobox_nvs_get_ntp_server());
  cJSON_AddStringToObject(root, "timezone", kaleidobox_nvs_get_clock_tz());

  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

static esp_err_t clock_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *item = cJSON_GetObjectItem(json, "mode");
  if (cJSON_IsString(item)) {
    uint8_t mode = 0;
    if (strcmp(item->valuestring, "outline") == 0) {
      mode = 1;
    } else if (strcmp(item->valuestring, "default") == 0) {
      mode = 2;
    } else if (strcmp(item->valuestring, "seethrough") == 0) {
      mode = 3;
    } else if (strcmp(item->valuestring, "rectangle") == 0) {
      mode = 4;
    }
    kaleidobox_nvs_set_clock_mode(mode);
  }

  item = cJSON_GetObjectItem(json, "color");
  if (cJSON_IsString(item) && item->valuestring[0] == '#' &&
      strlen(item->valuestring) == 7) {
    kaleidobox_nvs_set_clock_color(
        (uint32_t)strtoul(item->valuestring + 1, NULL, 16));
  }

  item = cJSON_GetObjectItem(json, "scale");
  if (cJSON_IsNumber(item) && item->valueint >= 1 && item->valueint <= 2) {
    kaleidobox_nvs_set_clock_scale((uint8_t)item->valueint);
  }

  item = cJSON_GetObjectItem(json, "24h");
  if (cJSON_IsBool(item)) {
    kaleidobox_nvs_set_clock_24h(cJSON_IsTrue(item));
  }

  bool ntp_changed = false;
  item = cJSON_GetObjectItem(json, "ntp_server");
  if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
    ntp_changed = kaleidobox_nvs_set_ntp_server(item->valuestring) == ESP_OK;
  }

  bool tz_changed = false;
  item = cJSON_GetObjectItem(json, "timezone");
  if (cJSON_IsString(item)) {
    tz_changed = kaleidobox_nvs_set_clock_tz(item->valuestring) == ESP_OK;
  }
  cJSON_Delete(json);

  // Applied live, not just persisted - a settings change should take
  // effect immediately, same as every other setting on this page.
  if (tz_changed) {
    kaleidobox_clock_apply_tz();
  }
  if (ntp_changed) {
    kaleidobox_clock_start_sntp();
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// --- PrintSpy (MQTT print-status takeover) ----------------------------------

static esp_err_t printspy_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "enabled", kaleidobox_nvs_get_printspy_enabled());
  cJSON_AddStringToObject(root, "broker", kaleidobox_nvs_get_mqtt_broker());
  cJSON_AddStringToObject(root, "user", kaleidobox_nvs_get_mqtt_user());
  // Password is write-only - never echoed back, same reasoning as this
  // device never echoing back the WiFi password. Just tells the UI
  // whether one's already set, so a blank field on save doesn't read as
  // "clear the password" when the user didn't touch it.
  cJSON_AddBoolToObject(root, "pass_set", kaleidobox_nvs_get_mqtt_pass()[0] != '\0');
  cJSON_AddStringToObject(root, "topic", kaleidobox_nvs_get_printspy_topic());

  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

static esp_err_t printspy_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid JSON");
    return ESP_FAIL;
  }

  // Snapshot every connection-relevant value before applying the POST -
  // stop()+start() below is a full MQTT disconnect/reconnect that also
  // wipes all currently-tracked printer state, so it's only worth doing
  // when something that actually matters to a live session changed, not
  // on every save (a no-op re-save, or an edit to an unrelated field).
  bool was_enabled = kaleidobox_nvs_get_printspy_enabled();
  char old_broker[80];
  strncpy(old_broker, kaleidobox_nvs_get_mqtt_broker(), sizeof(old_broker) - 1);
  old_broker[sizeof(old_broker) - 1] = '\0';
  char old_user[32];
  strncpy(old_user, kaleidobox_nvs_get_mqtt_user(), sizeof(old_user) - 1);
  old_user[sizeof(old_user) - 1] = '\0';
  char old_pass[32];
  strncpy(old_pass, kaleidobox_nvs_get_mqtt_pass(), sizeof(old_pass) - 1);
  old_pass[sizeof(old_pass) - 1] = '\0';
  char old_topic[80];
  strncpy(old_topic, kaleidobox_nvs_get_printspy_topic(), sizeof(old_topic) - 1);
  old_topic[sizeof(old_topic) - 1] = '\0';

  cJSON *item = cJSON_GetObjectItem(json, "enabled");
  if (cJSON_IsBool(item)) {
    kaleidobox_nvs_set_printspy_enabled(cJSON_IsTrue(item));
  }

  item = cJSON_GetObjectItem(json, "broker");
  if (cJSON_IsString(item)) {
    kaleidobox_nvs_set_mqtt_broker(item->valuestring);
  }

  item = cJSON_GetObjectItem(json, "user");
  if (cJSON_IsString(item)) {
    kaleidobox_nvs_set_mqtt_user(item->valuestring);
  }

  // Absent leaves the stored password untouched - see the GET handler's
  // pass_set comment. Present-but-empty is different: the UI only ever
  // sends "pass" at all when the user actually touched the field (see
  // settings.html), so an empty string here is a real "clear it"
  // request, not "nothing to do" - treating it the same as absent was a
  // real bug: clearing the field and saving silently kept the old
  // password, which then reappeared as dots on the next page load.
  item = cJSON_GetObjectItem(json, "pass");
  if (cJSON_IsString(item)) {
    kaleidobox_nvs_set_mqtt_pass(item->valuestring);
  }

  item = cJSON_GetObjectItem(json, "topic");
  if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
    kaleidobox_nvs_set_printspy_topic(item->valuestring);
  }

  cJSON_Delete(json);

  // Apply immediately, but only reconnect if something connection-
  // relevant actually changed - stop() is a no-op if nothing's
  // connected, and start() itself re-checks enabled/broker and no-ops
  // if either isn't set, but stop() also wipes the tracked-printer
  // table, so it's worth skipping entirely when nothing changed.
  // Turning "Enabled" off still actually disconnects right now (not
  // just "won't reconnect after the next reboot" - a real gap this used
  // to have), and any broker/credential/topic edit reconnects fresh
  // with the new values instead of waiting for the next boot.
  bool changed = was_enabled != kaleidobox_nvs_get_printspy_enabled() ||
                 strcmp(old_broker, kaleidobox_nvs_get_mqtt_broker()) != 0 ||
                 strcmp(old_user, kaleidobox_nvs_get_mqtt_user()) != 0 ||
                 strcmp(old_pass, kaleidobox_nvs_get_mqtt_pass()) != 0 ||
                 strcmp(old_topic, kaleidobox_nvs_get_printspy_topic()) != 0;
  if (changed) {
    kaleidobox_printspy_stop();
    kaleidobox_printspy_start();
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// --- Weather -----------------------------------------------------------------

static esp_err_t weather_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "enabled", kaleidobox_nvs_get_weather_enabled());
  // Plain text, not write-only - user explicitly wants to see it in the
  // UI rather than a masked/write-only field.
  cJSON_AddStringToObject(root, "key", kaleidobox_nvs_get_ow_api_key());
  cJSON_AddStringToObject(root, "zip", kaleidobox_nvs_get_weather_zip());
  cJSON_AddStringToObject(root, "units",
                          kaleidobox_nvs_get_weather_units() == 0 ? "metric" : "imperial");

  uint16_t fields = kaleidobox_nvs_get_weather_fields();
  cJSON *fields_obj = cJSON_AddObjectToObject(root, "fields");
  cJSON_AddBoolToObject(fields_obj, "temp", fields & WEATHER_FIELD_TEMP);
  cJSON_AddBoolToObject(fields_obj, "condition", fields & WEATHER_FIELD_CONDITION);
  cJSON_AddBoolToObject(fields_obj, "humidity", fields & WEATHER_FIELD_HUMIDITY);
  cJSON_AddBoolToObject(fields_obj, "wind", fields & WEATHER_FIELD_WIND);
  cJSON_AddBoolToObject(fields_obj, "feels_like", fields & WEATHER_FIELD_FEELS_LIKE);
  cJSON_AddBoolToObject(fields_obj, "high_low", fields & WEATHER_FIELD_HIGH_LOW);
  cJSON_AddBoolToObject(fields_obj, "location", fields & WEATHER_FIELD_LOCATION);

  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

static esp_err_t weather_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *item = cJSON_GetObjectItem(json, "enabled");
  if (cJSON_IsBool(item)) {
    kaleidobox_nvs_set_weather_enabled(cJSON_IsTrue(item));
  }

  item = cJSON_GetObjectItem(json, "key");
  if (cJSON_IsString(item)) {
    kaleidobox_nvs_set_ow_api_key(item->valuestring);
  }

  item = cJSON_GetObjectItem(json, "zip");
  if (cJSON_IsString(item)) {
    kaleidobox_nvs_set_weather_zip(item->valuestring);
  }

  item = cJSON_GetObjectItem(json, "units");
  if (cJSON_IsString(item)) {
    kaleidobox_nvs_set_weather_units(strcmp(item->valuestring, "metric") == 0 ? 0 : 1);
  }

  item = cJSON_GetObjectItem(json, "fields");
  if (cJSON_IsObject(item)) {
    uint16_t fields = 0;
    if (cJSON_IsTrue(cJSON_GetObjectItem(item, "temp"))) fields |= WEATHER_FIELD_TEMP;
    if (cJSON_IsTrue(cJSON_GetObjectItem(item, "condition"))) fields |= WEATHER_FIELD_CONDITION;
    if (cJSON_IsTrue(cJSON_GetObjectItem(item, "humidity"))) fields |= WEATHER_FIELD_HUMIDITY;
    if (cJSON_IsTrue(cJSON_GetObjectItem(item, "wind"))) fields |= WEATHER_FIELD_WIND;
    if (cJSON_IsTrue(cJSON_GetObjectItem(item, "feels_like"))) fields |= WEATHER_FIELD_FEELS_LIKE;
    if (cJSON_IsTrue(cJSON_GetObjectItem(item, "high_low"))) fields |= WEATHER_FIELD_HIGH_LOW;
    if (cJSON_IsTrue(cJSON_GetObjectItem(item, "location"))) fields |= WEATHER_FIELD_LOCATION;
    kaleidobox_nvs_set_weather_fields(fields);
  }

  cJSON_Delete(json);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// --- Display rotation --------------------------------------------------------
// See main/display_rotation.c - clock/printer/weather each get their own
// independent dwell time, so this lives in its own small endpoint rather
// than under any one of those three feature endpoints.

static esp_err_t rotation_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "clock_secs", kaleidobox_nvs_get_clock_secs());
  cJSON_AddNumberToObject(root, "printer_secs", kaleidobox_nvs_get_printer_secs());
  cJSON_AddNumberToObject(root, "weather_secs", kaleidobox_nvs_get_weather_secs());

  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

static esp_err_t rotation_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *item = cJSON_GetObjectItem(json, "clock_secs");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 300) {
    kaleidobox_nvs_set_clock_secs((uint16_t)item->valueint);
  }
  item = cJSON_GetObjectItem(json, "printer_secs");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 300) {
    kaleidobox_nvs_set_printer_secs((uint16_t)item->valueint);
  }
  item = cJSON_GetObjectItem(json, "weather_secs");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 300) {
    kaleidobox_nvs_set_weather_secs((uint16_t)item->valueint);
  }
  cJSON_Delete(json);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// --- Brightness ------------------------------------------------------------

static esp_err_t brightness_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "brightness", kaleidobox_nvs_get_brightness());
  cJSON_AddBoolToObject(root, "schedule_enabled",
                        kaleidobox_nvs_get_brightness_schedule_enabled());
  cJSON_AddNumberToObject(root, "dim_hour", kaleidobox_nvs_get_dim_hour());
  cJSON_AddNumberToObject(root, "dim_min", kaleidobox_nvs_get_dim_min());
  cJSON_AddNumberToObject(root, "dim_brightness", kaleidobox_nvs_get_dim_brightness());
  cJSON_AddNumberToObject(root, "bright_hour", kaleidobox_nvs_get_bright_hour());
  cJSON_AddNumberToObject(root, "bright_min", kaleidobox_nvs_get_bright_min());

  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

static esp_err_t brightness_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *item = cJSON_GetObjectItem(json, "brightness");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 255) {
    uint8_t brightness = (uint8_t)item->valueint;
    kaleidobox_nvs_set_brightness(brightness);
    kaleidobox_matrix_set_brightness(brightness);
  }

  item = cJSON_GetObjectItem(json, "schedule_enabled");
  if (cJSON_IsBool(item)) {
    kaleidobox_nvs_set_brightness_schedule_enabled(cJSON_IsTrue(item));
  }
  item = cJSON_GetObjectItem(json, "dim_hour");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 23) {
    kaleidobox_nvs_set_dim_hour((uint8_t)item->valueint);
  }
  item = cJSON_GetObjectItem(json, "dim_min");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 59) {
    kaleidobox_nvs_set_dim_min((uint8_t)item->valueint);
  }
  item = cJSON_GetObjectItem(json, "dim_brightness");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 255) {
    kaleidobox_nvs_set_dim_brightness((uint8_t)item->valueint);
  }
  item = cJSON_GetObjectItem(json, "bright_hour");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 23) {
    kaleidobox_nvs_set_bright_hour((uint8_t)item->valueint);
  }
  item = cJSON_GetObjectItem(json, "bright_min");
  if (cJSON_IsNumber(item) && item->valueint >= 0 && item->valueint <= 59) {
    kaleidobox_nvs_set_bright_min((uint8_t)item->valueint);
  }
  cJSON_Delete(json);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// --- Gallery ---------------------------------------------------------------
// Handlers report 404/501 whenever gallery.c's own calls do (typically:
// no TF card mounted, or - for next/prev - an empty gallery).

static esp_err_t gallery_get_handler(httpd_req_t *req) {
  char names[512];
  size_t count = kaleidobox_gallery_list(names, sizeof(names), 32);
  cJSON *root = cJSON_CreateObject();
  cJSON *arr = cJSON_CreateArray();
  if (count > 0) {
    char *saveptr = NULL;
    char *tok = strtok_r(names, "\n", &saveptr);
    while (tok) {
      cJSON_AddItemToArray(arr, cJSON_CreateString(tok));
      tok = strtok_r(NULL, "\n", &saveptr);
    }
  }
  cJSON_AddItemToObject(root, "images", arr);

  // cJSON's numbers are doubles - fine here, a double represents
  // integers exactly up to 2^53 (~9 petabytes), way past any TF card
  // this device will ever see. Fields just omitted (not zeroed) when
  // nothing's mounted, so the UI can tell "no card" from "empty card".
  uint64_t total_bytes = 0, free_bytes = 0;
  if (kaleidobox_sdcard_get_space(&total_bytes, &free_bytes) == ESP_OK) {
    cJSON_AddNumberToObject(root, "sdcard_total_bytes", (double)total_bytes);
    cJSON_AddNumberToObject(root, "sdcard_free_bytes", (double)free_bytes);
  }

  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

static esp_err_t gallery_save_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON *json = cJSON_Parse(body);
  free(body);
  const char *name = NULL;
  if (json) {
    cJSON *item = cJSON_GetObjectItem(json, "name");
    if (cJSON_IsString(item)) {
      name = item->valuestring;
    }
  }
  esp_err_t err = name ? kaleidobox_gallery_save(name) : ESP_ERR_INVALID_ARG;
  if (json) {
    cJSON_Delete(json);
  }

  if (err == ESP_ERR_INVALID_ARG) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"missing or invalid name\"}");
    return ESP_OK;
  } else if (err != ESP_OK) {
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_sendstr(req, "{\"error\":\"no TF card mounted\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t gallery_delete_handler(httpd_req_t *req) {
  static const char *prefix = "/api/gallery/";
  size_t prefix_len = strlen(prefix);
  if (strncmp(req->uri, prefix, prefix_len) != 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"missing name\"}");
    return ESP_OK;
  }
  // req->uri is just the path here (no query string on this route in
  // practice) - name runs to the end of the string.
  char name[64];
  url_decode_uri_tail(req->uri + prefix_len, name, sizeof(name));

  esp_err_t err = kaleidobox_gallery_delete(name);
  if (err == ESP_ERR_INVALID_ARG) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"invalid name\"}");
    return ESP_OK;
  } else if (err != ESP_OK) {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_sendstr(req, "{\"error\":\"not found\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t gallery_image_get_handler(httpd_req_t *req) {
  static const char *prefix = "/api/gallery/image/";
  size_t prefix_len = strlen(prefix);
  if (strncmp(req->uri, prefix, prefix_len) != 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"missing name\"}");
    return ESP_OK;
  }
  char name[64];
  url_decode_uri_tail(req->uri + prefix_len, name, sizeof(name));

  // Heap, not stack - same 12288-byte-on-8192-byte-httpd-task-stack
  // class of bug already caught (and fixed) elsewhere in this file.
  // MALLOC_CAP_SPIRAM, not plain malloc() - this is pure transfer data,
  // no DMA requirement, but under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL's
  // 16KB threshold a plain malloc() would still land it in the small
  // internal/DMA-capable heap regardless (same class of bug already
  // found and fixed in kaleidoscope.c's frame/copy buffers). A gallery
  // page loads several thumbnails concurrently, each one of these -
  // worth avoiding the same starvation risk here too.
  uint8_t *buf = heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * 3, MALLOC_CAP_SPIRAM);
  if (!buf) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  esp_err_t err = kaleidobox_gallery_read(name, buf);
  if (err != ESP_OK) {
    free(buf);
    httpd_resp_set_status(req, err == ESP_ERR_INVALID_ARG ? "400 Bad Request"
                                                           : "404 Not Found");
    httpd_resp_sendstr(req, "{\"error\":\"not found\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/octet-stream");
  esp_err_t res = httpd_resp_send(req, (const char *)buf, CANVAS_WIDTH * CANVAS_HEIGHT * 3);
  free(buf);
  return res;
}

static esp_err_t gallery_mode_get_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "auto_advance", kaleidobox_nvs_get_gallery_auto_advance());
  cJSON_AddNumberToObject(root, "interval_seconds",
                          kaleidobox_nvs_get_gallery_interval_seconds());
  char *json = cJSON_PrintUnformatted(root);
  httpd_resp_set_type(req, "application/json");
  esp_err_t res = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  cJSON_Delete(root);
  return res;
}

static esp_err_t gallery_mode_post_handler(httpd_req_t *req) {
  char *body = NULL;
  if (read_body(req, &body) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *item = cJSON_GetObjectItem(json, "auto_advance");
  if (cJSON_IsBool(item)) {
    kaleidobox_nvs_set_gallery_auto_advance(cJSON_IsTrue(item));
  }
  item = cJSON_GetObjectItem(json, "interval_seconds");
  if (cJSON_IsNumber(item) && item->valueint > 0 && item->valueint <= 3600) {
    kaleidobox_nvs_set_gallery_interval_seconds((uint16_t)item->valueint);
  }
  cJSON_Delete(json);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t gallery_next_handler(httpd_req_t *req) {
  esp_err_t err = kaleidobox_gallery_next();
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_sendstr(req, "{\"error\":\"no TF card mounted or gallery empty\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t gallery_prev_handler(httpd_req_t *req) {
  esp_err_t err = kaleidobox_gallery_prev();
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_sendstr(req, "{\"error\":\"no TF card mounted or gallery empty\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t gallery_show_post_handler(httpd_req_t *req) {
  static const char *prefix = "/api/gallery/show/";
  size_t prefix_len = strlen(prefix);
  if (strncmp(req->uri, prefix, prefix_len) != 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"missing name\"}");
    return ESP_OK;
  }
  char name[64];
  url_decode_uri_tail(req->uri + prefix_len, name, sizeof(name));

  esp_err_t err = kaleidobox_gallery_show(name);
  if (err == ESP_ERR_INVALID_ARG) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"invalid name\"}");
    return ESP_OK;
  } else if (err != ESP_OK) {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_sendstr(req, "{\"error\":\"not found\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// Decodes+resizes straight to a named gallery entry, never touching the
// live canvas.
static esp_err_t gallery_upload_post_handler(httpd_req_t *req) {
  static const char *prefix = "/api/gallery/upload/";
  size_t prefix_len = strlen(prefix);
  if (strncmp(req->uri, prefix, prefix_len) != 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"missing name\"}");
    return ESP_FAIL;
  }
  char name[64];
  url_decode_uri_tail(req->uri + prefix_len, name, sizeof(name));

  if (req->content_len == 0 || req->content_len > MAX_UPLOAD_BYTES) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"missing or oversized body\"}");
    return ESP_FAIL;
  }

  uint8_t *raw = malloc(req->content_len);
  if (!raw) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  size_t received = 0;
  while (received < req->content_len) {
    int r = httpd_req_recv(req, (char *)raw + received, req->content_len - received);
    if (r <= 0) {
      free(raw);
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    received += r;
  }

  kaleidobox_image_t img = {0};
  esp_err_t err = kaleidobox_image_decode(raw, req->content_len, &img);
  free(raw);
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"could not decode image - must be JPEG "
                            "or PNG, and small enough to decode on-device (8MB "
                            "cap; progressive JPEG/PNG need their full "
                            "resolution in memory regardless of display size)\"}");
    return ESP_FAIL;
  }

  // MALLOC_CAP_SPIRAM - see gallery_image_get_handler's comment above.
  uint8_t *resized = heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * 3, MALLOC_CAP_SPIRAM);
  if (!resized) {
    kaleidobox_image_free(&img);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  kaleidobox_image_resize_to_canvas(&img, resized);
  kaleidobox_image_free(&img);

  esp_err_t save_err = kaleidobox_gallery_save_bytes(name, resized);
  free(resized);

  if (save_err == ESP_ERR_INVALID_ARG) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"missing or invalid name\"}");
    return ESP_OK;
  } else if (save_err != ESP_OK) {
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_sendstr(req, "{\"error\":\"no TF card mounted\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

esp_err_t kaleidobox_http_server_start(void) {
  if (server) {
    return ESP_OK; // already running
  }

  if (log_worker_init() != ESP_OK) {
    ESP_LOGE(TAG, "failed to init log async worker");
    return ESP_ERR_NO_MEM;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  // HTTPD_DEFAULT_CONFIG() leaves core_id at tskNO_AFFINITY - unpinned,
  // free to land on core 1 alongside kaleidoscope (25fps, continuous),
  // gallery_bg_task, and display_rotation's rotation_task, all
  // deliberately pinned there (see their own xTaskCreatePinnedToCore
  // comments) specifically to stay off WiFi's core 0. This one request-
  // handling task serves every synchronous route (everything except
  // /api/logs, which already has its own log_worker_task) - if it lands
  // on core 1 it directly competes with those for CPU. Pin to core 0,
  // alongside WiFi/LWIP - this task fundamentally IS networking work.
  // (Real root cause of a since-fixed "everything is slow" regression
  // turned out to be socket-pool starvation, not this - see
  // max_open_sockets and the Cache-Control headers on the PNG handlers
  // above. This pin is still worth keeping on its own merits.)
  config.core_id = 0;
  // Default max_uri_handlers is 8 - we register more than that (root,
  // settings page, logo, icon, status, logs, wifi, ota, ws/draw, canvas
  // get/submit, kaleidoscope x2, clock x2, brightness x2, gallery x10,
  // printspy x2, weather x2, rotation x2). Past the cap,
  // httpd_register_uri_handler silently drops the excess - printspy-cam
  // hit this exact bug once already (see its http_server.c comment).
  config.max_uri_handlers = 33;
  // 1 for the log worker + 12 other concurrent connections. Confirmed
  // live: the old value (7 total, 6 available) had zero headroom over a
  // single browser tab's own per-origin connection limit - a couple of
  // slow image fetches (uncached PNGs, see logo_png_handler/
  // icon_512_png_handler above) could fully occupy it and stall every
  // other request, including plain JSON GETs, for 30s+. The real fix is
  // the Cache-Control headers so repeat loads don't re-fetch at all;
  // this is just headroom for the first, cold load.
  config.max_open_sockets = 13;
  config.lru_purge_enable = true;
  // Same reasoning as printspy-cam: without TCP keepalive, a stale
  // /api/logs (or /ws/draw) connection never gets detected as dead - it
  // just sits there holding a worker slot / socket forever.
  config.keep_alive_enable = true;
  config.keep_alive_idle = 5;
  config.keep_alive_interval = 5;
  config.keep_alive_count = 3;
  // DELETE /api/gallery/* registers a wildcard URI - without this, the
  // default matcher is a plain strcmp and that route never matches
  // anything.
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t err = httpd_start(&server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
    return err;
  }

  httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
  httpd_uri_t settings_page_uri = {
      .uri = "/settings", .method = HTTP_GET, .handler = settings_page_handler};
  httpd_uri_t logo_png_uri = {
      .uri = "/kaleidobox.png", .method = HTTP_GET, .handler = logo_png_handler};
  httpd_uri_t icon_512_png_uri = {.uri = "/icon-512.png",
                                  .method = HTTP_GET,
                                  .handler = icon_512_png_handler};
  httpd_uri_t status_uri = {
      .uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
  httpd_uri_t logs_uri = {
      .uri = "/api/logs", .method = HTTP_GET, .handler = logs_handler};
  httpd_uri_t wifi_uri = {
      .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler};
  httpd_uri_t ota_uri = {
      .uri = "/api/ota", .method = HTTP_POST, .handler = ota_post_handler};
  httpd_uri_t ws_draw_uri = {.uri = "/ws/draw",
                             .method = HTTP_GET,
                             .handler = ws_draw_handler,
                             .is_websocket = true};
  httpd_uri_t canvas_get_uri = {
      .uri = "/api/canvas", .method = HTTP_GET, .handler = canvas_get_handler};
  httpd_uri_t canvas_submit_uri = {.uri = "/api/canvas/submit",
                                   .method = HTTP_POST,
                                   .handler = canvas_submit_post_handler};
  httpd_uri_t kaleidoscope_get_uri = {.uri = "/api/kaleidoscope",
                                      .method = HTTP_GET,
                                      .handler = kaleidoscope_get_handler};
  httpd_uri_t kaleidoscope_post_uri = {.uri = "/api/kaleidoscope",
                                       .method = HTTP_POST,
                                       .handler = kaleidoscope_post_handler};
  httpd_uri_t clock_get_uri = {
      .uri = "/api/clock", .method = HTTP_GET, .handler = clock_get_handler};
  httpd_uri_t clock_post_uri = {
      .uri = "/api/clock", .method = HTTP_POST, .handler = clock_post_handler};
  httpd_uri_t printspy_get_uri = {.uri = "/api/printspy",
                                  .method = HTTP_GET,
                                  .handler = printspy_get_handler};
  httpd_uri_t printspy_post_uri = {.uri = "/api/printspy",
                                   .method = HTTP_POST,
                                   .handler = printspy_post_handler};
  httpd_uri_t weather_get_uri = {
      .uri = "/api/weather", .method = HTTP_GET, .handler = weather_get_handler};
  httpd_uri_t weather_post_uri = {.uri = "/api/weather",
                                  .method = HTTP_POST,
                                  .handler = weather_post_handler};
  httpd_uri_t rotation_get_uri = {
      .uri = "/api/rotation", .method = HTTP_GET, .handler = rotation_get_handler};
  httpd_uri_t rotation_post_uri = {.uri = "/api/rotation",
                                   .method = HTTP_POST,
                                   .handler = rotation_post_handler};
  httpd_uri_t brightness_get_uri = {.uri = "/api/brightness",
                                    .method = HTTP_GET,
                                    .handler = brightness_get_handler};
  httpd_uri_t brightness_post_uri = {.uri = "/api/brightness",
                                     .method = HTTP_POST,
                                     .handler = brightness_post_handler};
  httpd_uri_t gallery_get_uri = {
      .uri = "/api/gallery", .method = HTTP_GET, .handler = gallery_get_handler};
  httpd_uri_t gallery_save_uri = {.uri = "/api/gallery/save",
                                  .method = HTTP_POST,
                                  .handler = gallery_save_post_handler};
  httpd_uri_t gallery_delete_uri = {.uri = "/api/gallery/*",
                                    .method = HTTP_DELETE,
                                    .handler = gallery_delete_handler};
  httpd_uri_t gallery_image_uri = {.uri = "/api/gallery/image/*",
                                   .method = HTTP_GET,
                                   .handler = gallery_image_get_handler};
  httpd_uri_t gallery_mode_get_uri = {.uri = "/api/gallery/mode",
                                      .method = HTTP_GET,
                                      .handler = gallery_mode_get_handler};
  httpd_uri_t gallery_mode_uri = {.uri = "/api/gallery/mode",
                                  .method = HTTP_POST,
                                  .handler = gallery_mode_post_handler};
  httpd_uri_t gallery_next_uri = {.uri = "/api/gallery/next",
                                  .method = HTTP_POST,
                                  .handler = gallery_next_handler};
  httpd_uri_t gallery_prev_uri = {.uri = "/api/gallery/prev",
                                  .method = HTTP_POST,
                                  .handler = gallery_prev_handler};
  httpd_uri_t gallery_show_uri = {.uri = "/api/gallery/show/*",
                                  .method = HTTP_POST,
                                  .handler = gallery_show_post_handler};
  httpd_uri_t gallery_upload_uri = {.uri = "/api/gallery/upload/*",
                                    .method = HTTP_POST,
                                    .handler = gallery_upload_post_handler};

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &settings_page_uri);
  httpd_register_uri_handler(server, &logo_png_uri);
  httpd_register_uri_handler(server, &icon_512_png_uri);
  httpd_register_uri_handler(server, &status_uri);
  httpd_register_uri_handler(server, &logs_uri);
  httpd_register_uri_handler(server, &wifi_uri);
  httpd_register_uri_handler(server, &ota_uri);
  httpd_register_uri_handler(server, &ws_draw_uri);
  httpd_register_uri_handler(server, &canvas_get_uri);
  httpd_register_uri_handler(server, &canvas_submit_uri);
  httpd_register_uri_handler(server, &kaleidoscope_get_uri);
  httpd_register_uri_handler(server, &kaleidoscope_post_uri);
  httpd_register_uri_handler(server, &clock_get_uri);
  httpd_register_uri_handler(server, &clock_post_uri);
  httpd_register_uri_handler(server, &printspy_get_uri);
  httpd_register_uri_handler(server, &printspy_post_uri);
  httpd_register_uri_handler(server, &weather_get_uri);
  httpd_register_uri_handler(server, &weather_post_uri);
  httpd_register_uri_handler(server, &rotation_get_uri);
  httpd_register_uri_handler(server, &rotation_post_uri);
  httpd_register_uri_handler(server, &brightness_get_uri);
  httpd_register_uri_handler(server, &brightness_post_uri);
  httpd_register_uri_handler(server, &gallery_get_uri);
  httpd_register_uri_handler(server, &gallery_save_uri);
  httpd_register_uri_handler(server, &gallery_delete_uri);
  httpd_register_uri_handler(server, &gallery_image_uri);
  httpd_register_uri_handler(server, &gallery_mode_get_uri);
  httpd_register_uri_handler(server, &gallery_mode_uri);
  httpd_register_uri_handler(server, &gallery_next_uri);
  httpd_register_uri_handler(server, &gallery_prev_uri);
  httpd_register_uri_handler(server, &gallery_show_uri);
  httpd_register_uri_handler(server, &gallery_upload_uri);

  ESP_LOGI(TAG, "HTTP server started");
  return ESP_OK;
}

#include "http_server.h"

#include "canvas.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_netif.h"
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
#include "ota.h"
#include "settings.h"
#include "version.h"
#include "wifi.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "kaleidobox_http";

static httpd_handle_t server = NULL;

extern const uint8_t app_html_start[] asm("_binary_app_html_start");
extern const uint8_t app_html_end[] asm("_binary_app_html_end");

// Long-running handlers (SSE log console) block whichever task runs them
// until the client disconnects. esp_http_server services all connections
// from a single task by default, so without this, one open /api/logs
// would stall every other request until it closed. Ported from
// printspy-cam - see examples/protocols/http_server/async_handlers in
// esp-idf for Espressif's own version of this pattern.
typedef struct {
  QueueHandle_t queue;
  SemaphoreHandle_t free_slots;
} async_pool_t;

typedef struct {
  httpd_req_t *req;
  esp_err_t (*handler)(httpd_req_t *req);
} async_job_t;

static void async_worker_task(void *arg) {
  async_pool_t *pool = (async_pool_t *)arg;
  while (true) {
    xSemaphoreGive(pool->free_slots);
    async_job_t job;
    if (xQueueReceive(pool->queue, &job, portMAX_DELAY)) {
      job.handler(job.req);
      httpd_req_async_handler_complete(job.req);
    }
  }
}

static esp_err_t async_pool_init(async_pool_t *pool, int size,
                                 const char *name_prefix) {
  pool->free_slots = xSemaphoreCreateCounting(size, 0);
  pool->queue = xQueueCreate(size, sizeof(async_job_t));
  if (!pool->free_slots || !pool->queue) {
    return ESP_ERR_NO_MEM;
  }
  for (int i = 0; i < size; i++) {
    char task_name[20];
    snprintf(task_name, sizeof(task_name), "%s_%d", name_prefix, i);
    xTaskCreate(async_worker_task, task_name, 4096, pool,
               tskIDLE_PRIORITY + 1, NULL);
  }
  return ESP_OK;
}

static esp_err_t async_pool_dispatch(async_pool_t *pool, httpd_req_t *req,
                                     esp_err_t (*handler)(httpd_req_t *)) {
  httpd_req_t *copy = NULL;
  esp_err_t err = httpd_req_async_handler_begin(req, &copy);
  if (err != ESP_OK) {
    return err;
  }

  if (xSemaphoreTake(pool->free_slots, 0) != pdTRUE) {
    httpd_req_async_handler_complete(copy);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, "Too many concurrent connections");
    return ESP_OK;
  }

  async_job_t job = {.req = copy, .handler = handler};
  if (xQueueSend(pool->queue, &job, 0) != pdTRUE) {
    httpd_req_async_handler_complete(copy);
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, "Too many concurrent connections");
    return ESP_OK;
  }
  return ESP_OK;
}

#define LOG_WORKER_COUNT 1
static async_pool_t log_pool;

static esp_err_t root_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)app_html_start,
                         HTTPD_RESP_USE_STRLEN);
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
  return async_pool_dispatch(&log_pool, req, logs_async_handler);
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

// One WS text frame per pixel edit: {"x":n,"y":n,"r":n,"g":n,"b":n}.
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
  if (ret != ESP_OK || ws_pkt.len == 0 || ws_pkt.len > 128) {
    return ret;
  }

  char buf[129];
  ws_pkt.payload = (uint8_t *)buf;
  ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
  if (ret != ESP_OK) {
    return ret;
  }
  buf[ws_pkt.len] = '\0';

  cJSON *json = cJSON_Parse(buf);
  if (!json) {
    return ESP_OK; // drop malformed frame, keep the connection alive
  }
  cJSON *x = cJSON_GetObjectItem(json, "x");
  cJSON *y = cJSON_GetObjectItem(json, "y");
  cJSON *r = cJSON_GetObjectItem(json, "r");
  cJSON *g = cJSON_GetObjectItem(json, "g");
  cJSON *b = cJSON_GetObjectItem(json, "b");
  if (cJSON_IsNumber(x) && cJSON_IsNumber(y) && cJSON_IsNumber(r) &&
      cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
    kaleidobox_canvas_set_pixel((uint8_t)x->valueint, (uint8_t)y->valueint,
                               (uint8_t)r->valueint, (uint8_t)g->valueint,
                               (uint8_t)b->valueint);
  }
  cJSON_Delete(json);
  return ESP_OK;
}

// Draw-then-submit mode: raw CANVAS_WIDTH*CANVAS_HEIGHT*3 RGB888 body,
// one shot.
static esp_err_t canvas_submit_post_handler(httpd_req_t *req) {
  const size_t expected = CANVAS_WIDTH * CANVAS_HEIGHT * 3;
  if (req->content_len != expected) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "Body must be CANVAS_WIDTH*CANVAS_HEIGHT*3 bytes");
    return ESP_FAIL;
  }

  uint8_t buf[CANVAS_WIDTH * CANVAS_HEIGHT * 3];
  size_t received = 0;
  while (received < expected) {
    int r = httpd_req_recv(req, (char *)buf + received, expected - received);
    if (r <= 0) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    received += r;
  }

  for (int y = 0; y < CANVAS_HEIGHT; y++) {
    for (int x = 0; x < CANVAS_WIDTH; x++) {
      size_t idx = (y * CANVAS_WIDTH + x) * 3;
      kaleidobox_canvas_set_pixel(x, y, buf[idx], buf[idx + 1], buf[idx + 2]);
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// --- Upload --------------------------------------------------------------

// STUB - image_decode.c isn't implemented yet (see main/image_decode.c).
static esp_err_t upload_post_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "501 Not Implemented");
  httpd_resp_sendstr(req, "{\"error\":\"upload not yet implemented\"}");
  return ESP_OK;
}

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

// --- Gallery ---------------------------------------------------------------
// STUB - sdcard.c/gallery.c aren't implemented yet. These wire the real
// HTTP surface up now so the web app can be built against a stable API,
// but every handler reports 501 until the TF card is actually mounted.

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

  if (err != ESP_OK) {
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_sendstr(req, "{\"error\":\"gallery not yet implemented\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t gallery_delete_handler(httpd_req_t *req) {
  // Name comes from the URI tail (/api/gallery/<name>) - not yet parsed
  // since gallery.c has nothing to delete against.
  httpd_resp_set_status(req, "501 Not Implemented");
  httpd_resp_sendstr(req, "{\"error\":\"gallery not yet implemented\"}");
  return ESP_OK;
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
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_sendstr(req, "{\"error\":\"gallery not yet implemented\"}");
    return ESP_OK;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

static esp_err_t gallery_prev_handler(httpd_req_t *req) {
  esp_err_t err = kaleidobox_gallery_prev();
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_sendstr(req, "{\"error\":\"gallery not yet implemented\"}");
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

  if (async_pool_init(&log_pool, LOG_WORKER_COUNT, "log_worker") != ESP_OK) {
    ESP_LOGE(TAG, "failed to init async worker pool");
    return ESP_ERR_NO_MEM;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  // Default max_uri_handlers is 8 - we register more than that (root,
  // status, logs, wifi, ota, ws/draw, canvas/submit, upload,
  // kaleidoscope x2, gallery x6). Past the cap,
  // httpd_register_uri_handler silently drops the excess - printspy-cam
  // hit this exact bug once already (see its http_server.c comment).
  config.max_uri_handlers = 16;
  config.max_open_sockets = LOG_WORKER_COUNT + 6;
  config.lru_purge_enable = true;
  // Same reasoning as printspy-cam: without TCP keepalive, a stale
  // /api/logs (or /ws/draw) connection never gets detected as dead - it
  // just sits there holding a worker slot / socket forever.
  config.keep_alive_enable = true;
  config.keep_alive_idle = 5;
  config.keep_alive_interval = 5;
  config.keep_alive_count = 3;

  esp_err_t err = httpd_start(&server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
    return err;
  }

  httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
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
  httpd_uri_t canvas_submit_uri = {.uri = "/api/canvas/submit",
                                   .method = HTTP_POST,
                                   .handler = canvas_submit_post_handler};
  httpd_uri_t upload_uri = {
      .uri = "/api/upload", .method = HTTP_POST, .handler = upload_post_handler};
  httpd_uri_t kaleidoscope_get_uri = {.uri = "/api/kaleidoscope",
                                      .method = HTTP_GET,
                                      .handler = kaleidoscope_get_handler};
  httpd_uri_t kaleidoscope_post_uri = {.uri = "/api/kaleidoscope",
                                       .method = HTTP_POST,
                                       .handler = kaleidoscope_post_handler};
  httpd_uri_t gallery_get_uri = {
      .uri = "/api/gallery", .method = HTTP_GET, .handler = gallery_get_handler};
  httpd_uri_t gallery_save_uri = {.uri = "/api/gallery/save",
                                  .method = HTTP_POST,
                                  .handler = gallery_save_post_handler};
  httpd_uri_t gallery_delete_uri = {.uri = "/api/gallery/*",
                                    .method = HTTP_DELETE,
                                    .handler = gallery_delete_handler};
  httpd_uri_t gallery_mode_uri = {.uri = "/api/gallery/mode",
                                  .method = HTTP_POST,
                                  .handler = gallery_mode_post_handler};
  httpd_uri_t gallery_next_uri = {.uri = "/api/gallery/next",
                                  .method = HTTP_POST,
                                  .handler = gallery_next_handler};
  httpd_uri_t gallery_prev_uri = {.uri = "/api/gallery/prev",
                                  .method = HTTP_POST,
                                  .handler = gallery_prev_handler};

  httpd_register_uri_handler(server, &root_uri);
  httpd_register_uri_handler(server, &status_uri);
  httpd_register_uri_handler(server, &logs_uri);
  httpd_register_uri_handler(server, &wifi_uri);
  httpd_register_uri_handler(server, &ota_uri);
  httpd_register_uri_handler(server, &ws_draw_uri);
  httpd_register_uri_handler(server, &canvas_submit_uri);
  httpd_register_uri_handler(server, &upload_uri);
  httpd_register_uri_handler(server, &kaleidoscope_get_uri);
  httpd_register_uri_handler(server, &kaleidoscope_post_uri);
  httpd_register_uri_handler(server, &gallery_get_uri);
  httpd_register_uri_handler(server, &gallery_save_uri);
  httpd_register_uri_handler(server, &gallery_delete_uri);
  httpd_register_uri_handler(server, &gallery_mode_uri);
  httpd_register_uri_handler(server, &gallery_next_uri);
  httpd_register_uri_handler(server, &gallery_prev_uri);

  ESP_LOGI(TAG, "HTTP server started");
  return ESP_OK;
}

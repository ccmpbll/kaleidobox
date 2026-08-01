#include "weather.h"

#include "cJSON.h"
#include "canvas.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "font_5x7.h"
#include "matrix.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "weather";

#define RESPONSE_BUF_SIZE 4096

typedef struct {
  bool valid;
  char city[48];
  char condition[32];
  int temp;
  int feels_like;
  int temp_min;
  int temp_max;
  int humidity;
  float wind_speed;
} weather_data_t;

// esp_http_client's own timeout (10s) is generous enough that a slow/dead
// endpoint doesn't wedge the caller forever. This is the first outbound
// HTTPS client in this firmware - TLS handshakes are stack-hungry, which
// is why the caller (display_rotation.c's rotation task) uses an 8192
// stack rather than a typical background-task size.

static bool fetch_weather(weather_data_t *out) {
  const char *zip = kaleidobox_nvs_get_weather_zip();
  const char *key = kaleidobox_nvs_get_ow_api_key();
  if (!zip[0] || !key[0]) {
    return false;
  }
  uint8_t units_val = kaleidobox_nvs_get_weather_units();
  const char *units = units_val == 0 ? "metric" : "imperial";

  // OWM's zip= param defaults to US when no country code is given -
  // confirmed live: "zip=29621" and "zip=29621,US" both resolve to the
  // same place. No country field needed.
  char url[256];
  snprintf(url, sizeof(url),
          "https://api.openweathermap.org/data/2.5/weather?zip=%s&appid=%s&units=%s",
          zip, key, units);

  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 10000,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }
  if (esp_http_client_open(client, 0) != ESP_OK) {
    esp_http_client_cleanup(client);
    return false;
  }
  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);

  // MALLOC_CAP_SPIRAM - same internal-RAM-starvation class already
  // fixed elsewhere: 4KB is under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL's
  // threshold, and this is held for the whole HTTPS round-trip -
  // concurrently with the TLS handshake's own real memory needs, the
  // exact overlap this project has already hit once
  // (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC's own history).
  char *body = heap_caps_malloc(RESPONSE_BUF_SIZE, MALLOC_CAP_SPIRAM);
  if (!body) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }
  size_t total = 0;
  int r;
  while (total < RESPONSE_BUF_SIZE - 1 &&
         (r = esp_http_client_read(client, body + total, RESPONSE_BUF_SIZE - 1 - total)) > 0) {
    total += r;
  }
  body[total] = '\0';
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (status != 200) {
    ESP_LOGW(TAG, "OpenWeatherMap returned HTTP %d", status);
    free(body);
    return false;
  }

  cJSON *json = cJSON_Parse(body);
  free(body);
  if (!json) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  cJSON *name = cJSON_GetObjectItem(json, "name");
  if (cJSON_IsString(name)) {
    strncpy(out->city, name->valuestring, sizeof(out->city) - 1);
  }
  cJSON *weather_arr = cJSON_GetObjectItem(json, "weather");
  if (cJSON_IsArray(weather_arr) && cJSON_GetArraySize(weather_arr) > 0) {
    cJSON *first = cJSON_GetArrayItem(weather_arr, 0);
    cJSON *main = cJSON_GetObjectItem(first, "main");
    if (cJSON_IsString(main)) {
      strncpy(out->condition, main->valuestring, sizeof(out->condition) - 1);
    }
  }
  cJSON *main_obj = cJSON_GetObjectItem(json, "main");
  if (cJSON_IsObject(main_obj)) {
    cJSON *temp = cJSON_GetObjectItem(main_obj, "temp");
    if (cJSON_IsNumber(temp)) {
      out->temp = (int)(temp->valuedouble + 0.5);
    }
    cJSON *feels = cJSON_GetObjectItem(main_obj, "feels_like");
    if (cJSON_IsNumber(feels)) {
      out->feels_like = (int)(feels->valuedouble + 0.5);
    }
    cJSON *tmin = cJSON_GetObjectItem(main_obj, "temp_min");
    if (cJSON_IsNumber(tmin)) {
      out->temp_min = (int)(tmin->valuedouble + 0.5);
    }
    cJSON *tmax = cJSON_GetObjectItem(main_obj, "temp_max");
    if (cJSON_IsNumber(tmax)) {
      out->temp_max = (int)(tmax->valuedouble + 0.5);
    }
    cJSON *humidity = cJSON_GetObjectItem(main_obj, "humidity");
    if (cJSON_IsNumber(humidity)) {
      out->humidity = humidity->valueint;
    }
  }
  cJSON *wind_obj = cJSON_GetObjectItem(json, "wind");
  if (cJSON_IsObject(wind_obj)) {
    cJSON *speed = cJSON_GetObjectItem(wind_obj, "speed");
    if (cJSON_IsNumber(speed)) {
      out->wind_speed = (float)speed->valuedouble;
    }
  }
  cJSON_Delete(json);
  out->valid = true;
  return true;
}

typedef struct {
  char text[24];
  uint8_t r, g, b;
} weather_line_t;

static void render_weather(const weather_data_t *w) {
  uint16_t fields = kaleidobox_nvs_get_weather_fields();
  uint8_t units_val = kaleidobox_nvs_get_weather_units();
  char deg = units_val == 0 ? 'C' : 'F';

  kaleidobox_matrix_clear();
  int y = 3;
  if (fields & WEATHER_FIELD_LOCATION) {
    if (!kaleidobox_font_draw_text_fit(y, w->city, 255, 255, 255)) {
      kaleidobox_font_draw_text_centered(y, "Weather", 255, 255, 255);
    }
    y += 11;
  }

  // Build the field list first, then space rows to fit whatever's left
  // of the panel below the city - with all 6 fields on, a fixed 9px
  // step ran the last row (wind) off the bottom of the 64px panel
  // (confirmed live: "it doesn't all fit on the screen"). Never widen
  // past 9px (the original spacing, still right for fewer fields) -
  // only tighten when there isn't room.
  weather_line_t lines[6];
  int n = 0;
  if (fields & WEATHER_FIELD_CONDITION && w->condition[0]) {
    strncpy(lines[n].text, w->condition, sizeof(lines[n].text) - 1);
    lines[n].text[sizeof(lines[n].text) - 1] = '\0';
    lines[n].r = 150; lines[n].g = 200; lines[n].b = 255;
    n++;
  }
  if (fields & WEATHER_FIELD_TEMP) {
    snprintf(lines[n].text, sizeof(lines[n].text), "%d%c", w->temp, deg);
    lines[n].r = 255; lines[n].g = 200; lines[n].b = 80;
    n++;
  }
  if (fields & WEATHER_FIELD_FEELS_LIKE) {
    snprintf(lines[n].text, sizeof(lines[n].text), "Feels %d%c", w->feels_like, deg);
    lines[n].r = 200; lines[n].g = 200; lines[n].b = 200;
    n++;
  }
  if (fields & WEATHER_FIELD_HIGH_LOW) {
    snprintf(lines[n].text, sizeof(lines[n].text), "H%d L%d", w->temp_max, w->temp_min);
    lines[n].r = 200; lines[n].g = 200; lines[n].b = 200;
    n++;
  }
  if (fields & WEATHER_FIELD_HUMIDITY) {
    snprintf(lines[n].text, sizeof(lines[n].text), "%d%% humid", w->humidity);
    lines[n].r = 150; lines[n].g = 150; lines[n].b = 200;
    n++;
  }
  if (fields & WEATHER_FIELD_WIND) {
    snprintf(lines[n].text, sizeof(lines[n].text), "%.0f%s wind", w->wind_speed,
            units_val == 0 ? "m/s" : "mph");
    lines[n].r = 150; lines[n].g = 150; lines[n].b = 200;
    n++;
  }

  if (n == 0) {
    return;
  }
  // Fixed 9px step (7px glyph + 2px gap) - shrinking it to guarantee
  // every enabled field fits was the wrong fix: with 6 fields the step
  // collapsed to 7px, exactly the glyph height, so rows touched with no
  // gap at all ("squashed together", confirmed live). Better to drop
  // whichever trailing fields don't fit than render all of them
  // illegibly.
  const int step = 9;
  int max_n = (CANVAS_HEIGHT - y - 7) / step + 1;
  if (n > max_n) {
    ESP_LOGW(TAG, "weather: %d fields enabled, only %d fit - dropping the rest", n, max_n);
    n = max_n;
  }
  for (int i = 0; i < n; i++) {
    kaleidobox_font_draw_text_centered(y, lines[i].text, lines[i].r, lines[i].g, lines[i].b);
    y += step;
  }
}

static weather_data_t g_last_fetch;
static bool g_last_fetch_valid = false;

// Fetches a fresh OpenWeatherMap reading into an internal buffer.
// Deliberately does NOT touch the matrix or the panel takeover - the
// caller (main/display_rotation.c) needs to know whether there's
// actually something to show *before* it takes the panel away from
// kaleidoscope, so it can skip the takeover entirely on a failed fetch
// instead of stopping/restarting kaleidoscope for nothing (a real bug:
// every failed fetch was still doing a full takeover begin/end pair,
// which visibly restarts kaleidoscope from its static source buffer -
// looks like a blink even though the image never actually changes).
bool kaleidobox_weather_fetch(void) {
  g_last_fetch_valid = fetch_weather(&g_last_fetch);
  if (!g_last_fetch_valid) {
    ESP_LOGW(TAG, "weather fetch failed");
  }
  return g_last_fetch_valid;
}

// Renders the data from the most recent successful
// kaleidobox_weather_fetch() call directly to the matrix. No-op if
// that call hasn't happened yet or didn't succeed.
void kaleidobox_weather_render_last(void) {
  if (g_last_fetch_valid) {
    render_weather(&g_last_fetch);
  }
}

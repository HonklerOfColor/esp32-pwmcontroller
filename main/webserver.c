#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"

#include "app_config.h"
#include "night_mode.h"
#include "webserver.h"

static const char *TAG = "webserver";

/* Embedded index.html (built by CMake EMBED_FILES) */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

/* ─── Minimal JSON helpers ───────────────────────────────────────────────── */

/**
 * Find the value string that follows "key": in a flat JSON object.
 * Returns a pointer into `json` pointing at the first non-whitespace
 * character of the value, or NULL if the key is absent.
 */
static const char *json_find(const char *json, const char *key)
{
    char needle[64];
    int  len = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (len <= 0) return NULL;

    const char *p = strstr(json, needle);
    if (!p) return NULL;
    p += len;
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    return p;
}

static bool json_get_bool(const char *json, const char *key, bool *out)
{
    const char *p = json_find(json, key);
    if (!p) return false;
    if (strncmp(p, "true",  4) == 0) { *out = true;  return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool json_get_float(const char *json, const char *key, float *out)
{
    const char *p = json_find(json, key);
    if (!p) return false;
    char *end;
    float v = strtof(p, &end);
    if (end == p) return false;
    *out = v;
    return true;
}

static bool json_get_int(const char *json, const char *key, int *out)
{
    const char *p = json_find(json, key);
    if (!p) return false;
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    *out = (int)v;
    return true;
}

static bool json_get_int64(const char *json, const char *key, int64_t *out)
{
    const char *p = json_find(json, key);
    if (!p) return false;
    char *end;
    long long v = strtoll(p, &end, 10);
    if (end == p) return false;
    *out = (int64_t)v;
    return true;
}

/* ─── HTTP helpers ───────────────────────────────────────────────────────── */

#define RECV_BUF_SIZE 512

static int recv_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    if (req->content_len == 0 || req->content_len >= buf_size) {
        ESP_LOGW(TAG, "Body too large or empty: %d", req->content_len);
        return -1;
    }
    int received = 0;
    int remaining = (int)req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf + received, remaining);
        if (r < 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return -1;
        }
        received  += r;
        remaining -= r;
    }
    buf[received] = '\0';
    return received;
}

static void send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json);
}

/* ─── GET / ─────────────────────────────────────────────────────────────── */

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    size_t html_len = (size_t)(index_html_end - index_html_start);
    httpd_resp_send(req, (const char *)index_html_start, (ssize_t)html_len);
    return ESP_OK;
}

/* ─── GET /api/status ────────────────────────────────────────────────────── */

static esp_err_t handler_status(httpd_req_t *req)
{
    char time_str[24];
    night_mode_get_time_str(time_str, sizeof(time_str));

    if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
        "{"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"fan_pct\":%.1f,"
        "\"fan_target\":%.1f,"
        "\"auto_mode\":%s,"
        "\"manual_pct\":%u,"
        "\"temp_low\":%.1f,"
        "\"temp_high\":%.1f,"
        "\"fan_min\":%u,"
        "\"fan_max\":%u,"
        "\"night_enabled\":%s,"
        "\"night_start\":%u,"
        "\"night_end\":%u,"
        "\"night_max\":%u,"
        "\"night_active\":%s,"
        "\"time\":\"%s\""
        "}",
        g_state.temperature,
        g_state.humidity,
        g_state.fan_current_pct,
        g_state.fan_target_pct,
        g_state.auto_mode     ? "true" : "false",
        g_state.manual_pct,
        g_state.temp_low,
        g_state.temp_high,
        g_state.fan_min,
        g_state.fan_max,
        g_state.night_enabled ? "true" : "false",
        g_state.night_start,
        g_state.night_end,
        g_state.night_max,
        g_state.night_active  ? "true" : "false",
        time_str
    );

    xSemaphoreGive(g_state.mutex);
    send_json(req, buf);
    return ESP_OK;
}

/* ─── POST /api/control ──────────────────────────────────────────────────── */

static esp_err_t handler_control(httpd_req_t *req)
{
    char body[RECV_BUF_SIZE];
    if (recv_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body error");
        return ESP_FAIL;
    }

    bool  auto_mode  = true;
    bool  has_mode   = json_get_bool(body, "auto_mode", &auto_mode);
    int   manual_pct = 50;
    bool  has_pct    = json_get_int(body, "manual_pct", &manual_pct);

    if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (has_mode) g_state.auto_mode = auto_mode;
        if (has_pct) {
            if (manual_pct < 0)   manual_pct = 0;
            if (manual_pct > 100) manual_pct = 100;
            g_state.manual_pct = (uint8_t)manual_pct;
            if (!g_state.auto_mode) {
                g_state.fan_target_pct = (float)manual_pct;
            }
        }
        xSemaphoreGive(g_state.mutex);
    }

    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ─── POST /api/config ───────────────────────────────────────────────────── */

static esp_err_t handler_config(httpd_req_t *req)
{
    char body[RECV_BUF_SIZE];
    if (recv_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body error");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        float f;
        int   i;
        bool  b;

        if (json_get_float(body, "temp_low",  &f)) g_state.temp_low  = f;
        if (json_get_float(body, "temp_high", &f)) g_state.temp_high = f;

        if (json_get_int(body, "fan_min", &i)) {
            if (i < 0)   { i = 0;   }
            if (i > 100) { i = 100; }
            g_state.fan_min = (uint8_t)i;
        }
        if (json_get_int(body, "fan_max", &i)) {
            if (i < 1)   { i = 1;   }
            if (i > 100) { i = 100; }
            g_state.fan_max = (uint8_t)i;
        }
        if (json_get_bool(body, "night_enabled", &b)) { g_state.night_enabled = b; }
        if (json_get_int(body, "night_start", &i)) {
            if (i < 0)  { i = 0;  }
            if (i > 23) { i = 23; }
            g_state.night_start = (uint8_t)i;
        }
        if (json_get_int(body, "night_end", &i)) {
            if (i < 0)  { i = 0;  }
            if (i > 23) { i = 23; }
            g_state.night_end = (uint8_t)i;
        }
        if (json_get_int(body, "night_max", &i)) {
            if (i < 0)   { i = 0;   }
            if (i > 100) { i = 100; }
            g_state.night_max = (uint8_t)i;
        }

        xSemaphoreGive(g_state.mutex);
    }

    ESP_LOGI(TAG, "Config updated");
    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ─── POST /api/time ─────────────────────────────────────────────────────── */

static esp_err_t handler_time(httpd_req_t *req)
{
    char body[RECV_BUF_SIZE];
    if (recv_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body error");
        return ESP_FAIL;
    }

    int64_t epoch_ms = 0;
    if (json_get_int64(body, "epoch_ms", &epoch_ms)) {
        night_mode_set_time(epoch_ms);
    }

    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ─── Captive portal detection handlers ──────────────────────────────────── */
/*
 * Different OS families probe different URLs to detect captive portals.
 * We redirect every probe to our dashboard so the OS shows a "Sign in" prompt.
 *
 * iOS / macOS  → GET /hotspot-detect.html  (via captive.apple.com)
 * Android      → GET /generate_204         (expects HTTP 204)
 * Windows      → GET /ncsi.txt             (expects "Microsoft NCSI")
 *               GET /connecttest.txt       (expects "Microsoft Connect Test")
 */

#define CAPTIVE_REDIRECT "http://" WIFI_AP_IP "/"

static esp_err_t handler_captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", CAPTIVE_REDIRECT);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Android expects exactly HTTP 204 – we redirect instead, which also works */
static esp_err_t handler_generate_204(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", CAPTIVE_REDIRECT);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Catch-all 404 → redirect to dashboard (handles any unknown hostname/path) */
static esp_err_t handler_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", CAPTIVE_REDIRECT);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ─── Server startup ─────────────────────────────────────────────────────── */

void webserver_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 16;   /* more slots for captive portal endpoints */
    cfg.stack_size       = 8192;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    /* Application endpoints */
    static const httpd_uri_t uris[] = {
        { .uri = "/",                   .method = HTTP_GET,  .handler = handler_root             },
        { .uri = "/api/status",         .method = HTTP_GET,  .handler = handler_status           },
        { .uri = "/api/control",        .method = HTTP_POST, .handler = handler_control          },
        { .uri = "/api/config",         .method = HTTP_POST, .handler = handler_config           },
        { .uri = "/api/time",           .method = HTTP_POST, .handler = handler_time             },
        /* Captive portal probes */
        { .uri = "/generate_204",       .method = HTTP_GET,  .handler = handler_generate_204     },
        { .uri = "/hotspot-detect.html",.method = HTTP_GET,  .handler = handler_captive_redirect },
        { .uri = "/ncsi.txt",           .method = HTTP_GET,  .handler = handler_captive_redirect },
        { .uri = "/connecttest.txt",    .method = HTTP_GET,  .handler = handler_captive_redirect },
        { .uri = "/redirect",           .method = HTTP_GET,  .handler = handler_captive_redirect },
        { .uri = "/canonical.html",     .method = HTTP_GET,  .handler = handler_captive_redirect },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uris[i]));
    }

    /* Redirect all unknown paths to the dashboard */
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, handler_404);

    ESP_LOGI(TAG, "HTTP server running  (captive portal active)");
}

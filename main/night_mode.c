#include <time.h>
#include <sys/time.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "night_mode.h"

static const char *TAG = "night_mode";

/* ─── Helpers ────────────────────────────────────────────────────────────── */

/**
 * Returns true if 'hour' is inside the night window [start, end).
 * Handles wrap-around midnight (e.g. start=22, end=7).
 */
static bool is_in_night_window(int hour, uint8_t start, uint8_t end)
{
    if (start >= end) {
        /* Wraps midnight: e.g. 22:00 – 07:00 */
        return (hour >= start) || (hour < end);
    }
    /* Same day: e.g. 01:00 – 06:00 */
    return (hour >= start) && (hour < end);
}

/* ─── Public helpers ─────────────────────────────────────────────────────── */

void night_mode_set_time(int64_t epoch_ms)
{
    struct timeval tv = {
        .tv_sec  = (time_t)(epoch_ms / 1000),
        .tv_usec = (suseconds_t)((epoch_ms % 1000) * 1000),
    };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "System time updated from browser  epoch_ms=%lld", epoch_ms);
}

void night_mode_get_time_str(char *buf, size_t len)
{
    time_t    now;
    struct tm tm_info;
    time(&now);
    localtime_r(&now, &tm_info);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm_info);
}

/* ─── Task ───────────────────────────────────────────────────────────────── */

static void night_mode_task(void *pvParam)
{
    (void)pvParam;

    for (;;) {
        time_t    now;
        struct tm tm_info;
        time(&now);
        localtime_r(&now, &tm_info);

        int hour = tm_info.tm_hour;

        if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool active = g_state.night_enabled &&
                          is_in_night_window(hour,
                                             g_state.night_start,
                                             g_state.night_end);
            if (active != g_state.night_active) {
                g_state.night_active = active;
                ESP_LOGI(TAG, "Night mode %s  (hour=%d)", active ? "ON" : "OFF", hour);
            }
            xSemaphoreGive(g_state.mutex);
        }

        /* Check every minute */
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void night_mode_start_task(void)
{
    xTaskCreate(night_mode_task, "night_mode", 2048, NULL, 2, NULL);
    ESP_LOGD(TAG, "Night-mode task started");
}

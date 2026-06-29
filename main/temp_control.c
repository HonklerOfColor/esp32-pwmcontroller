#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "temp_control.h"

static const char *TAG = "temp_ctrl";

#define CTRL_INTERVAL_MS 5000

/* ─── Core algorithm ─────────────────────────────────────────────────────── */

/**
 * Linear interpolation:
 *   temp ≤ temp_low  →  fan_min
 *   temp ≥ temp_high →  fan_max
 *   in between       →  proportional
 */
static float compute_auto_speed(float temp, float t_low, float t_high,
                                 uint8_t f_min, uint8_t f_max)
{
    if (temp <= t_low)  return (float)f_min;
    if (temp >= t_high) return (float)f_max;

    float frac = (temp - t_low) / (t_high - t_low);
    return (float)f_min + frac * (float)(f_max - f_min);
}

/* ─── Task ───────────────────────────────────────────────────────────────── */

static void ctrl_task(void *pvParam)
{
    (void)pvParam;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CTRL_INTERVAL_MS));

        if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(50)) != pdTRUE) continue;

        float   temp          = g_state.temperature;
        float   t_low         = g_state.temp_low;
        float   t_high        = g_state.temp_high;
        uint8_t f_min         = g_state.fan_min;
        uint8_t f_max         = g_state.fan_max;
        bool    auto_mode     = g_state.auto_mode;
        uint8_t manual_pct    = g_state.manual_pct;
        bool    night_active  = g_state.night_active;
        bool    night_enabled = g_state.night_enabled;
        uint8_t night_max     = g_state.night_max;

        float target;
        if (auto_mode) {
            target = compute_auto_speed(temp, t_low, t_high, f_min, f_max);
            ESP_LOGD(TAG, "Auto: T=%.1f°C → target=%.0f%%", temp, target);
        } else {
            target = (float)manual_pct;
            ESP_LOGD(TAG, "Manual: target=%.0f%%", target);
        }

        /* Night-mode cap */
        if (night_enabled && night_active && target > (float)night_max) {
            ESP_LOGD(TAG, "Night cap: %.0f%% → %u%%", target, night_max);
            target = (float)night_max;
        }

        g_state.fan_target_pct = target;

        xSemaphoreGive(g_state.mutex);

        ESP_LOGI(TAG, "Fan target → %.0f%%  (auto=%d night=%d)",
                 target, auto_mode, night_enabled && night_active);
    }
}

void temp_control_start_task(void)
{
    xTaskCreate(ctrl_task, "temp_ctrl", 3072, NULL, 3, NULL);
    ESP_LOGD(TAG, "Temp-control task started  interval=%d ms", CTRL_INTERVAL_MS);
}

#include <math.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "pwm_control.h"

static const char *TAG = "pwm";

/* ─── Helpers ────────────────────────────────────────────────────────────── */

static uint32_t pct_to_duty(float pct)
{
    if (pct <= 0.0f) return 0u;
    if (pct >= 100.0f) return PWM_DUTY_MAX;
    return (uint32_t)((pct / 100.0f) * PWM_DUTY_MAX + 0.5f);
}

static void apply_duty(float pct)
{
    uint32_t duty = pct_to_duty(pct);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL);
}

/* ─── Public: init ───────────────────────────────────────────────────────── */

void pwm_init(void)
{
    /* Timer: 25 kHz, 10-bit resolution */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = PWM_RESOLUTION_BITS,
        .timer_num       = PWM_TIMER,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* Channel: GPIO 16, starts at 0 % */
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = PWM_CHANNEL,
        .timer_sel  = PWM_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ESP_LOGD(TAG, "LEDC ready  %d Hz  10-bit  GPIO%d", PWM_FREQ_HZ, PWM_GPIO);
}

/* ─── Public: set target ─────────────────────────────────────────────────── */

void pwm_set_target(uint8_t percent)
{
    if (percent > 100u) percent = 100u;

    if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_state.fan_target_pct = (float)percent;
        xSemaphoreGive(g_state.mutex);
    }
}

/* ─── Ramp task ──────────────────────────────────────────────────────────── */

static void ramp_task(void *pvParam)
{
    (void)pvParam;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(RAMP_INTERVAL_MS));

        if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(10)) != pdTRUE) continue;

        float current = g_state.fan_current_pct;
        float target  = g_state.fan_target_pct;

        if (fabsf(current - target) > RAMP_STEP_PCT) {
            current += (target > current) ? RAMP_STEP_PCT : -RAMP_STEP_PCT;
        } else {
            current = target;
        }

        /* Clamp */
        if (current < 0.0f)   current = 0.0f;
        if (current > 100.0f) current = 100.0f;

        g_state.fan_current_pct = current;
        xSemaphoreGive(g_state.mutex);

        apply_duty(current);
    }
}

void pwm_start_ramp_task(void)
{
    xTaskCreate(ramp_task, "pwm_ramp", 2048, NULL, 5, NULL);
    ESP_LOGD(TAG, "Ramp task started  step=%.1f %%  interval=%d ms",
             RAMP_STEP_PCT, RAMP_INTERVAL_MS);
}

/*
 * Fan tachometer – counts falling edges on the fan tach pin (Pin 3).
 *
 * The ISR increments a volatile counter protected by a FreeRTOS spinlock.
 * A task wakes every TACH_SAMPLE_MS, reads+resets the counter and computes
 * RPM from: RPM = (pulses / TACH_PULSES_PER_REV) * (60 000 / TACH_SAMPLE_MS)
 *
 * Arctic P14 Pro PST: 2 pulses per revolution → at max ~1700 RPM the ISR
 * fires ~57 times/s, well within interrupt budget.
 */

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

#include "app_config.h"
#include "tach.h"

static const char *TAG = "tach";

/* ─── ISR-safe pulse counter ─────────────────────────────────────────────── */

static volatile uint32_t s_pulses = 0;
static portMUX_TYPE       s_mux   = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR tach_isr(void *arg)
{
    portENTER_CRITICAL_ISR(&s_mux);
    s_pulses++;
    portEXIT_CRITICAL_ISR(&s_mux);
}

/* ─── Init ───────────────────────────────────────────────────────────────── */

esp_err_t tach_init(void)
{
    gpio_config_t io = {
        .intr_type    = GPIO_INTR_NEGEDGE,      /* tach pulls LOW on each pulse */
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << TACH_GPIO),
        .pull_up_en   = GPIO_PULLUP_ENABLE,     /* external 4.7 kΩ recommended  */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio_config");

    /* gpio_install_isr_service may already be called by another driver */
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "isr_service");
    }

    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add(TACH_GPIO, tach_isr, NULL), TAG, "isr_add");

    ESP_LOGI(TAG, "Tach ready  GPIO%d  %d pulses/rev", TACH_GPIO, TACH_PULSES_PER_REV);
    return ESP_OK;
}

/* ─── Sampling task ──────────────────────────────────────────────────────── */

static void tach_task(void *pvParam)
{
    (void)pvParam;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TACH_SAMPLE_MS));

        /* Atomically read and reset the pulse counter */
        portENTER_CRITICAL(&s_mux);
        uint32_t pulses = s_pulses;
        s_pulses = 0;
        portEXIT_CRITICAL(&s_mux);

        /* RPM = pulses / pulses_per_rev / (sample_ms/60000) */
        uint32_t rpm = pulses * (60000u / (TACH_PULSES_PER_REV * TACH_SAMPLE_MS));

        if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_state.fan_rpm = rpm;
            xSemaphoreGive(g_state.mutex);
        }

        ESP_LOGD(TAG, "pulses=%"PRIu32"  rpm=%"PRIu32, pulses, rpm);
    }
}

void tach_start_task(void)
{
    xTaskCreate(tach_task, "tach", 2048, NULL, 4, NULL);
    ESP_LOGD(TAG, "Tach task started  sample=%d ms", TACH_SAMPLE_MS);
}

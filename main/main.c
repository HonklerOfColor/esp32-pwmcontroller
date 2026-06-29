#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "wifi_ap.h"
#include "pwm_control.h"
#include "bme280.h"
#include "temp_control.h"
#include "night_mode.h"
#include "webserver.h"

static const char *TAG = "main";

/* Global shared state */
app_state_t g_state;

static void state_init(void)
{
    g_state.mutex = xSemaphoreCreateMutex();
    configASSERT(g_state.mutex);

    g_state.temperature     = 0.0f;
    g_state.humidity        = 0.0f;
    g_state.fan_current_pct = 0.0f;
    g_state.fan_target_pct  = 0.0f;
    g_state.auto_mode       = true;
    g_state.manual_pct      = 50u;

    g_state.temp_low        = DEFAULT_TEMP_LOW;
    g_state.temp_high       = DEFAULT_TEMP_HIGH;
    g_state.fan_min         = DEFAULT_FAN_MIN;
    g_state.fan_max         = DEFAULT_FAN_MAX;

    g_state.night_enabled   = DEFAULT_NIGHT_ENABLED;
    g_state.night_start     = DEFAULT_NIGHT_START;
    g_state.night_end       = DEFAULT_NIGHT_END;
    g_state.night_max       = DEFAULT_NIGHT_MAX;
    g_state.night_active    = false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 Fan Controller booting...");

    /* NVS – required for WiFi driver */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase – erasing and re-initialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    state_init();
    ESP_LOGI(TAG, "State initialised");

    wifi_ap_init();
    ESP_LOGI(TAG, "WiFi AP started  SSID='%s'  IP=%s", WIFI_SSID, WIFI_AP_IP);

    pwm_init();
    ESP_LOGI(TAG, "PWM initialised  GPIO%d  %d Hz", PWM_GPIO, PWM_FREQ_HZ);

    bme280_init();
    ESP_LOGI(TAG, "BME280 initialised  I2C addr=0x%02X", BME280_ADDR);

    /* Start background tasks */
    pwm_start_ramp_task();
    bme280_start_task();
    temp_control_start_task();
    night_mode_start_task();

    /* HTTP server is started last so all state is ready */
    webserver_start();

    ESP_LOGI(TAG, "All systems running – open http://%s in a browser", WIFI_AP_IP);
}

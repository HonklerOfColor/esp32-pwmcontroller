/*
 * Minimal BME280 driver – temperature + humidity only.
 * Uses the ESP-IDF v6 I2C master API (driver/i2c_master.h).
 * Compensation formulas from Bosch BST-BME280-DS002 datasheet.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "bme280.h"

static const char *TAG = "bme280";

/* ─── BME280 register addresses ─────────────────────────────────────────── */
#define REG_CHIP_ID     0xD0
#define REG_RESET       0xE0
#define REG_CTRL_HUM    0xF2
#define REG_CTRL_MEAS   0xF4
#define REG_CONFIG      0xF5
#define REG_DATA        0xF7    /* 0xF7..0xFE: press(3)+temp(3)+hum(2) */
#define REG_CALIB_00    0x88
#define REG_DIG_H1      0xA1
#define REG_CALIB_H     0xE1

#define BME280_CHIP_ID  0x60

/* ─── Calibration data ───────────────────────────────────────────────────── */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
} bme280_calib_t;

static bme280_calib_t           s_calib;
static int32_t                  s_t_fine;
static i2c_master_dev_handle_t  s_dev;

/* ─── Low-level I2C helpers (new IDF v6 API) ─────────────────────────────── */

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 100);
}

/* ─── Calibration loading ────────────────────────────────────────────────── */

static esp_err_t load_calibration(void)
{
    uint8_t raw[24];
    ESP_RETURN_ON_ERROR(reg_read(REG_CALIB_00, raw, 24), TAG, "calib00");

    s_calib.dig_T1 = (uint16_t)(raw[1] << 8 | raw[0]);
    s_calib.dig_T2 = (int16_t) (raw[3] << 8 | raw[2]);
    s_calib.dig_T3 = (int16_t) (raw[5] << 8 | raw[4]);

    uint8_t h1;
    ESP_RETURN_ON_ERROR(reg_read(REG_DIG_H1, &h1, 1), TAG, "H1");
    s_calib.dig_H1 = h1;

    uint8_t hraw[7];
    ESP_RETURN_ON_ERROR(reg_read(REG_CALIB_H, hraw, 7), TAG, "calib_H");

    s_calib.dig_H2 = (int16_t)(hraw[1] << 8 | hraw[0]);
    s_calib.dig_H3 = hraw[2];
    s_calib.dig_H4 = (int16_t)((int16_t)(hraw[3] << 4) | (hraw[4] & 0x0F));
    s_calib.dig_H5 = (int16_t)((int16_t)(hraw[5] << 4) | (hraw[4] >> 4));
    s_calib.dig_H6 = (int8_t)hraw[6];

    ESP_LOGD(TAG, "T1=%u T2=%d T3=%d H1=%u H2=%d H3=%u H4=%d H5=%d H6=%d",
             s_calib.dig_T1, s_calib.dig_T2, s_calib.dig_T3,
             s_calib.dig_H1, s_calib.dig_H2, s_calib.dig_H3,
             s_calib.dig_H4, s_calib.dig_H5, s_calib.dig_H6);
    return ESP_OK;
}

/* ─── Compensation (Bosch datasheet integer arithmetic) ─────────────────── */

static int32_t compensate_temperature(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) *
                    (int32_t)s_calib.dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)s_calib.dig_T1) *
                       ((adc_T >> 4) - (int32_t)s_calib.dig_T1)) >> 12) *
                    (int32_t)s_calib.dig_T3) >> 14;
    s_t_fine = var1 + var2;
    return (s_t_fine * 5 + 128) >> 8;   /* 0.01 °C */
}

static uint32_t compensate_humidity(int32_t adc_H)
{
    int32_t x = s_t_fine - (int32_t)76800;
    x = (((((adc_H << 14) - ((int32_t)s_calib.dig_H4 << 20) -
            ((int32_t)s_calib.dig_H5 * x)) + (int32_t)32768) >> 15) *
         (((((((x * (int32_t)s_calib.dig_H6) >> 10) *
              (((x * (int32_t)s_calib.dig_H3) >> 11) + (int32_t)32768)) >> 10) +
             (int32_t)2097152) * (int32_t)s_calib.dig_H2 + 8192) >> 14));
    x -= (((x >> 15) * (x >> 15)) >> 7) * (int32_t)s_calib.dig_H1 >> 4;
    if (x < 0) x = 0;
    if (x > 419430400) x = 419430400;
    return (uint32_t)(x >> 12);  /* Q22.10, divide by 1024 for %RH */
}

/* ─── Public: init ───────────────────────────────────────────────────────── */

esp_err_t bme280_init(void)
{
    /* I2C master bus */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source            = I2C_CLK_SRC_DEFAULT,
        .i2c_port              = I2C_PORT,
        .scl_io_num            = I2C_SCL_GPIO,
        .sda_io_num            = I2C_SDA_GPIO,
        .glitch_ignore_cnt     = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "bus_init");

    /* BME280 device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BME280_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev),
                        TAG, "dev_add");

    /* Verify chip ID */
    uint8_t chip_id = 0;
    ESP_RETURN_ON_ERROR(reg_read(REG_CHIP_ID, &chip_id, 1), TAG, "chip_id");
    if (chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected chip ID 0x%02X (expected 0x%02X)", chip_id, BME280_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "BME280 found (chip_id=0x%02X)", chip_id);

    /* Soft reset */
    ESP_RETURN_ON_ERROR(reg_write(REG_RESET, 0xB6), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Calibration */
    ESP_RETURN_ON_ERROR(load_calibration(), TAG, "calib");

    /* Humidity oversampling ×1 (must be written before ctrl_meas) */
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL_HUM, 0x01), TAG, "ctrl_hum");

    /* Temp ×1 + pressure ×1 + normal mode */
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL_MEAS, 0x27), TAG, "ctrl_meas");

    /* Standby 1000 ms, no IIR filter */
    ESP_RETURN_ON_ERROR(reg_write(REG_CONFIG, 0xA0), TAG, "config");

    return ESP_OK;
}

/* ─── Sensor read ────────────────────────────────────────────────────────── */

static esp_err_t bme280_read(float *temperature, float *humidity)
{
    uint8_t raw[8];
    ESP_RETURN_ON_ERROR(reg_read(REG_DATA, raw, 8), TAG, "data");

    /* raw[0-2]: pressure (ignored), raw[3-5]: temperature, raw[6-7]: humidity */
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8)  |  (int32_t)raw[7];

    *temperature = (float)compensate_temperature(adc_T) / 100.0f;
    *humidity    = (float)compensate_humidity(adc_H)    / 1024.0f;
    return ESP_OK;
}

/* ─── Sensor task ────────────────────────────────────────────────────────── */

static void sensor_task(void *pvParam)
{
    (void)pvParam;
    float temp, hum;

    for (;;) {
        if (bme280_read(&temp, &hum) == ESP_OK) {
            ESP_LOGD(TAG, "T=%.2f°C  H=%.2f%%", temp, hum);
            if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_state.temperature = temp;
                g_state.humidity    = hum;
                xSemaphoreGive(g_state.mutex);
            }
        } else {
            ESP_LOGW(TAG, "Read failed – retrying");
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
    }
}

void bme280_start_task(void)
{
    xTaskCreate(sensor_task, "bme280", 4096, NULL, 4, NULL);
}

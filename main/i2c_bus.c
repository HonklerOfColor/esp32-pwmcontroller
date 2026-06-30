#include "esp_log.h"
#include "driver/i2c_master.h"

#include "app_config.h"
#include "i2c_bus.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .clk_source            = I2C_CLK_SRC_DEFAULT,
        .i2c_port              = I2C_PORT,
        .scl_io_num            = I2C_SCL_GPIO,
        .sda_io_num            = I2C_SDA_GPIO,
        .glitch_ignore_cnt     = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&cfg, &s_bus);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C master bus ready  SDA=GPIO%d  SCL=GPIO%d  %d Hz",
                 I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_FREQ_HZ);
    }
    return ret;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus;
}

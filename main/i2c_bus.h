#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * Initialise the shared I²C master bus (GPIO 21/22).
 * Must be called once before bme280_init() and oled_init().
 */
esp_err_t i2c_bus_init(void);

/**
 * Return the shared bus handle.
 * Returns NULL if i2c_bus_init() has not been called yet.
 */
i2c_master_bus_handle_t i2c_bus_get_handle(void);

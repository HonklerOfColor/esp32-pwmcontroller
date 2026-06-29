#pragma once

#include "esp_err.h"

/**
 * Initialise I2C bus and configure the BME280 for continuous reading.
 * Reads chip-ID, loads calibration data, sets oversampling + normal mode.
 */
esp_err_t bme280_init(void);

/**
 * Start the FreeRTOS task that reads BME280 every SENSOR_INTERVAL_MS and
 * writes temperature + humidity into g_state (mutex-protected).
 */
void bme280_start_task(void);

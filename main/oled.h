#pragma once

#include "esp_err.h"

/**
 * Initialise the SSD1306 128×64 OLED on the shared I²C bus.
 * Must be called after i2c_bus_init().
 */
esp_err_t oled_init(void);

/**
 * Start the FreeRTOS task that refreshes the display every ~2.5 s.
 * Reads temperature, humidity, fan speed and mode from g_state.
 *
 * Layout (128×64):
 *   Page 0  │ Status bar: mode (AUTO/MANU) + time (HH:MM)
 *   Page 1-2│ Temperature – large 2× font  e.g. "  23.4°C  "
 *   Page 3  │ Humidity                      e.g. "Feuchte: 52%"
 *   Page 4  │ Fan speed (text)              e.g. "Luefter: 64%"
 *   Page 5  │ Fan speed bar  [#######     ]
 */
void oled_start_task(void);

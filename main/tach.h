#pragma once

#include "esp_err.h"

/**
 * Initialise the tachometer input on TACH_GPIO (app_config.h).
 *
 * Configures the GPIO as input with internal pull-up and installs a
 * falling-edge interrupt that counts pulses from the fan's open-collector
 * tach output (Pin 3, green wire).
 *
 * Arctic P14 Pro PST: 2 pulses per revolution.
 * Connect Pin 3 of ONE fan through a 4.7 kΩ pull-up to 3V3, then to
 * TACH_GPIO.  The PST tach line is NOT shared – read only one fan.
 */
esp_err_t tach_init(void);

/**
 * Start the FreeRTOS task that samples the pulse counter every second
 * and writes the computed RPM into g_state.fan_rpm.
 */
void tach_start_task(void);

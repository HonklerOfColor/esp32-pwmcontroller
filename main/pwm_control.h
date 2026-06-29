#pragma once

#include <stdint.h>

/**
 * Initialise the LEDC timer and channel for 25 kHz PWM on PWM_GPIO.
 * Must be called once before starting any task.
 */
void pwm_init(void);

/**
 * Set the desired fan speed (0–100 %).
 * The ramp task will smoothly transition fan_current_pct toward this value.
 * Thread-safe – uses g_state mutex internally.
 */
void pwm_set_target(uint8_t percent);

/**
 * Start the FreeRTOS ramp task.
 * Every RAMP_INTERVAL_MS the current duty is moved RAMP_STEP_PCT toward the target.
 */
void pwm_start_ramp_task(void);

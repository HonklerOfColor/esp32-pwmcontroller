#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ─── Hardware pins ────────────────────────────────────────────────────────── */
#define PWM_GPIO            16
#define I2C_SDA_GPIO        21
#define I2C_SCL_GPIO        22
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         400000

/* ─── LEDC / PWM ───────────────────────────────────────────────────────────── */
#define PWM_FREQ_HZ         25000
#define PWM_RESOLUTION_BITS LEDC_TIMER_10_BIT
#define PWM_TIMER           LEDC_TIMER_0
#define PWM_CHANNEL         LEDC_CHANNEL_0
#define PWM_DUTY_MAX        1023u   /* 2^10 - 1 */

/* ─── Ramp ─────────────────────────────────────────────────────────────────── */
#define RAMP_INTERVAL_MS    20      /* task wake interval */
#define RAMP_STEP_PCT       0.5f   /* % per interval → 0→100 in ~4 s */

/* ─── Kickstart (cold-start from 0 %) ─────────────────────────────────────── */
/* When the fan is stopped and a non-zero target is set, briefly run at        */
/* KICKSTART_PCT to overcome motor inertia, then ramp to the real target.      */
#define KICKSTART_PCT       60u     /* % during kickstart pulse               */
#define KICKSTART_MS        400u    /* duration of the kickstart pulse (ms)   */

/* ─── Sensor ───────────────────────────────────────────────────────────────── */
#define BME280_ADDR         0x76
#define SENSOR_INTERVAL_MS  2000

/* ─── WiFi AP ──────────────────────────────────────────────────────────────── */
#define WIFI_SSID           "espFanControl"
#define WIFI_PASS           "P4ssw0rt!"
#define WIFI_CHANNEL        6
#define WIFI_MAX_CLIENTS    4
#define WIFI_AP_IP          "192.168.4.1"

/* ─── Default operating parameters ────────────────────────────────────────── */
#define DEFAULT_TEMP_LOW        22.0f   /* fan runs at minimum below this */
#define DEFAULT_TEMP_HIGH       30.0f   /* fan runs at maximum above this */
#define DEFAULT_FAN_MIN         20u     /* % – minimum running speed     */
#define DEFAULT_FAN_MAX         100u    /* % – maximum speed             */
#define DEFAULT_NIGHT_ENABLED   true
#define DEFAULT_NIGHT_START     22u     /* 22:00 */
#define DEFAULT_NIGHT_END       7u      /* 07:00 */
#define DEFAULT_NIGHT_MAX       50u     /* % max during night            */

/* ─── Shared application state ─────────────────────────────────────────────── */
typedef struct {
    /* Live sensor readings */
    float    temperature;
    float    humidity;

    /* Fan state */
    float    fan_current_pct;  /* actual duty, changed by ramp task */
    float    fan_target_pct;   /* desired duty, written by control logic */
    bool     auto_mode;        /* true = temperature-driven */
    uint8_t  manual_pct;       /* setpoint used in manual mode */

    /* Temperature control config */
    float    temp_low;         /* fan at fan_min below this temp */
    float    temp_high;        /* fan at fan_max above this temp */
    uint8_t  fan_min;          /* % */
    uint8_t  fan_max;          /* % */

    /* Night-mode config */
    bool     night_enabled;
    uint8_t  night_start;      /* hour 0-23 */
    uint8_t  night_end;        /* hour 0-23 */
    uint8_t  night_max;        /* % cap during night */
    bool     night_active;     /* set by night_mode task */

    /* Thread safety */
    SemaphoreHandle_t mutex;
} app_state_t;

/* Single global state instance, defined in main.c */
extern app_state_t g_state;

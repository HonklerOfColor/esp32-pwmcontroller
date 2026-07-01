/*
 * SSD1306 128×64 OLED driver (I²C, ESP-IDF v6 master API).
 *
 * Uses a full 1024-byte framebuffer that is flushed to the display
 * after every render cycle.  All drawing is done in software.
 *
 * Font: 5×7 bitmap, 6 columns per character (5 data + 1 gap).
 * Large mode: 2× scaled → 12 columns × 16 rows per character.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "i2c_bus.h"
#include "night_mode.h"
#include "oled.h"

static const char *TAG = "oled";

/* ─── SSD1306 I²C address and commands ─────────────────────────────────── */
#define OLED_ADDR        0x3C
#define OLED_CMD_BYTE    0x00   /* control byte: command stream  */
#define OLED_DATA_BYTE   0x40   /* control byte: data stream     */

/* ─── Display dimensions ────────────────────────────────────────────────── */
#define OLED_W      128
#define OLED_H_PX   64
#define OLED_PAGES  8           /* 64 / 8 = 8 pages               */

/* ─── 5×7 ASCII font (chars 0x20–0x7F, byte = column, bit0 = top row) ─── */
/* Last entry (0x7F) is a custom degree ° symbol.                           */
static const uint8_t font5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 20   */  {0x00,0x00,0x5F,0x00,0x00}, /* 21 ! */
    {0x00,0x07,0x00,0x07,0x00}, /* 22 " */  {0x14,0x7F,0x14,0x7F,0x14}, /* 23 # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 24 $ */  {0x23,0x13,0x08,0x64,0x62}, /* 25 % */
    {0x36,0x49,0x55,0x22,0x50}, /* 26 & */  {0x00,0x05,0x03,0x00,0x00}, /* 27 ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 28 ( */  {0x00,0x41,0x22,0x1C,0x00}, /* 29 ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* 2A * */  {0x08,0x08,0x3E,0x08,0x08}, /* 2B + */
    {0x00,0x50,0x30,0x00,0x00}, /* 2C , */  {0x08,0x08,0x08,0x08,0x08}, /* 2D - */
    {0x00,0x60,0x60,0x00,0x00}, /* 2E . */  {0x20,0x10,0x08,0x04,0x02}, /* 2F / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 30 0 */  {0x00,0x42,0x7F,0x40,0x00}, /* 31 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 32 2 */  {0x21,0x41,0x45,0x4B,0x31}, /* 33 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 34 4 */  {0x27,0x45,0x45,0x45,0x39}, /* 35 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 36 6 */  {0x01,0x71,0x09,0x05,0x03}, /* 37 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 38 8 */  {0x06,0x49,0x49,0x29,0x1E}, /* 39 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* 3A : */  {0x00,0x56,0x36,0x00,0x00}, /* 3B ; */
    {0x08,0x14,0x22,0x41,0x00}, /* 3C < */  {0x14,0x14,0x14,0x14,0x14}, /* 3D = */
    {0x00,0x41,0x22,0x14,0x08}, /* 3E > */  {0x02,0x01,0x51,0x09,0x06}, /* 3F ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* 40 @ */  {0x7E,0x11,0x11,0x11,0x7E}, /* 41 A */
    {0x7F,0x49,0x49,0x49,0x36}, /* 42 B */  {0x3E,0x41,0x41,0x41,0x22}, /* 43 C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 44 D */  {0x7F,0x49,0x49,0x49,0x41}, /* 45 E */
    {0x7F,0x09,0x09,0x09,0x01}, /* 46 F */  {0x3E,0x41,0x49,0x49,0x7A}, /* 47 G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 48 H */  {0x00,0x41,0x7F,0x41,0x00}, /* 49 I */
    {0x20,0x40,0x41,0x3F,0x01}, /* 4A J */  {0x7F,0x08,0x14,0x22,0x41}, /* 4B K */
    {0x7F,0x40,0x40,0x40,0x40}, /* 4C L */  {0x7F,0x02,0x0C,0x02,0x7F}, /* 4D M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 4E N */  {0x3E,0x41,0x41,0x41,0x3E}, /* 4F O */
    {0x7F,0x09,0x09,0x09,0x06}, /* 50 P */  {0x3E,0x41,0x51,0x21,0x5E}, /* 51 Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* 52 R */  {0x46,0x49,0x49,0x49,0x31}, /* 53 S */
    {0x01,0x01,0x7F,0x01,0x01}, /* 54 T */  {0x3F,0x40,0x40,0x40,0x3F}, /* 55 U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 56 V */  {0x3F,0x40,0x38,0x40,0x3F}, /* 57 W */
    {0x63,0x14,0x08,0x14,0x63}, /* 58 X */  {0x07,0x08,0x70,0x08,0x07}, /* 59 Y */
    {0x61,0x51,0x49,0x45,0x43}, /* 5A Z */  {0x00,0x7F,0x41,0x41,0x00}, /* 5B [ */
    {0x02,0x04,0x08,0x10,0x20}, /* 5C \ */  {0x00,0x41,0x41,0x7F,0x00}, /* 5D ] */
    {0x04,0x02,0x01,0x02,0x04}, /* 5E ^ */  {0x40,0x40,0x40,0x40,0x40}, /* 5F _ */
    {0x00,0x01,0x02,0x04,0x00}, /* 60 ` */  {0x20,0x54,0x54,0x54,0x78}, /* 61 a */
    {0x7F,0x48,0x44,0x44,0x38}, /* 62 b */  {0x38,0x44,0x44,0x44,0x20}, /* 63 c */
    {0x38,0x44,0x44,0x48,0x7F}, /* 64 d */  {0x38,0x54,0x54,0x54,0x18}, /* 65 e */
    {0x08,0x7E,0x09,0x01,0x02}, /* 66 f */  {0x0C,0x52,0x52,0x52,0x3E}, /* 67 g */
    {0x7F,0x08,0x04,0x04,0x78}, /* 68 h */  {0x00,0x44,0x7D,0x40,0x00}, /* 69 i */
    {0x20,0x40,0x44,0x3D,0x00}, /* 6A j */  {0x7F,0x10,0x28,0x44,0x00}, /* 6B k */
    {0x00,0x41,0x7F,0x40,0x00}, /* 6C l */  {0x7C,0x04,0x18,0x04,0x78}, /* 6D m */
    {0x7C,0x08,0x04,0x04,0x78}, /* 6E n */  {0x38,0x44,0x44,0x44,0x38}, /* 6F o */
    {0x7C,0x14,0x14,0x14,0x08}, /* 70 p */  {0x08,0x14,0x14,0x18,0x7C}, /* 71 q */
    {0x7C,0x08,0x04,0x04,0x08}, /* 72 r */  {0x48,0x54,0x54,0x54,0x20}, /* 73 s */
    {0x04,0x3F,0x44,0x40,0x20}, /* 74 t */  {0x3C,0x40,0x40,0x20,0x7C}, /* 75 u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 76 v */  {0x3C,0x40,0x30,0x40,0x3C}, /* 77 w */
    {0x44,0x28,0x10,0x28,0x44}, /* 78 x */  {0x0C,0x50,0x50,0x50,0x3C}, /* 79 y */
    {0x44,0x64,0x54,0x4C,0x44}, /* 7A z */  {0x00,0x08,0x36,0x41,0x00}, /* 7B { */
    {0x00,0x00,0x7F,0x00,0x00}, /* 7C | */  {0x00,0x41,0x36,0x08,0x00}, /* 7D } */
    {0x10,0x08,0x08,0x10,0x08}, /* 7E ~ */  {0x00,0x02,0x05,0x02,0x00}, /* 7F ° */
};

/* ─── Framebuffer and I²C device ────────────────────────────────────────── */
static uint8_t                 s_fb[OLED_PAGES * OLED_W]; /* 1024 bytes */
static uint8_t                 s_tx[1 + OLED_PAGES * OLED_W]; /* prefix + fb */
static i2c_master_dev_handle_t s_dev;

/* ─── I²C helpers ────────────────────────────────────────────────────────── */

static esp_err_t ssd_cmd(const uint8_t *data, size_t n)
{
    uint8_t buf[18];
    if (n > sizeof(buf) - 1) return ESP_ERR_INVALID_ARG;
    buf[0] = OLED_CMD_BYTE;
    memcpy(buf + 1, data, n);
    return i2c_master_transmit(s_dev, buf, n + 1, 200);
}

static esp_err_t fb_flush(void)
{
    /* Set column 0→127, page 0→7 */
    const uint8_t addr[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    esp_err_t ret = ssd_cmd(addr, sizeof(addr));
    if (ret != ESP_OK) return ret;

    /* Send entire framebuffer in one transaction */
    s_tx[0] = OLED_DATA_BYTE;
    memcpy(s_tx + 1, s_fb, sizeof(s_fb));
    return i2c_master_transmit(s_dev, s_tx, sizeof(s_tx), 500);
}

/* ─── Drawing primitives ─────────────────────────────────────────────────── */

static inline void fb_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

/* Set or clear a single pixel at (x, y). y=0 is the top row. */
static inline void fb_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H_PX) return;
    int page = y / 8;
    int bit  = y % 8;
    if (on) {
        s_fb[page * OLED_W + x] |=  (uint8_t)(1u << bit);
    } else {
        s_fb[page * OLED_W + x] &= ~(uint8_t)(1u << bit);
    }
}

/* Draw a single character at pixel column x, page row p (1× scale = 6×8 px). */
static void fb_char_s(int x, int page, char c)
{
    if (c < 0x20 || c > 0x7F) c = '?';
    const uint8_t *g = font5x7[(uint8_t)c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 8; row++) {
            fb_pixel(x + col, page * 8 + row, (bits >> row) & 1);
        }
    }
    /* column 5 = gap (already 0 from fb_clear) */
}

/* Draw a null-terminated string at (x, page), 1× scale. */
static void fb_str_s(int x, int page, const char *s)
{
    while (*s) {
        fb_char_s(x, page, *s++);
        x += 6;
        if (x > OLED_W) break;
    }
}

/* Draw a single character at pixel (x, page) with 2× scale → 12×16 px. */
static void fb_char_l(int x, int page, char c)
{
    if (c < 0x20 || c > 0x7F) c = '?';
    const uint8_t *g = font5x7[(uint8_t)c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; row++) {
            if ((bits >> row) & 1) {
                int px = x + col * 2;
                int py = page * 8 + row * 2;
                fb_pixel(px,     py,     true);
                fb_pixel(px + 1, py,     true);
                fb_pixel(px,     py + 1, true);
                fb_pixel(px + 1, py + 1, true);
            }
        }
        /* 2-pixel column gap */
    }
}

/* Draw a null-terminated string at (x, page) with 2× scale. */
static void fb_str_l(int x, int page, const char *s)
{
    while (*s) {
        fb_char_l(x, page, *s++);
        x += 12;
        if (x > OLED_W) break;
    }
}

/* Draw a horizontal line of pixels across the full display width. */
static void fb_hline(int y)
{
    for (int x = 0; x < OLED_W; x++) {
        fb_pixel(x, y, true);
    }
}

/* Draw a filled progress bar.
   page    : page row (8 pixels tall)
   y_off   : y offset within the page (0-7)
   h       : bar height in pixels
   filled  : 0-100 percentage fill */
static void fb_bar(int page, int y_off, int h, int filled_pct)
{
    int fill_px = (OLED_W - 4) * filled_pct / 100; /* 2px margin each side */
    for (int y = y_off; y < y_off + h; y++) {
        int abs_y = page * 8 + y;
        /* border left & right */
        fb_pixel(1,           abs_y, true);
        fb_pixel(OLED_W - 2,  abs_y, true);
        /* fill */
        for (int x = 2; x < 2 + fill_px; x++) {
            fb_pixel(x, abs_y, true);
        }
    }
    /* top and bottom border lines */
    for (int x = 1; x < OLED_W - 1; x++) {
        fb_pixel(x, page * 8 + y_off,         true);
        fb_pixel(x, page * 8 + y_off + h - 1, true);
    }
}

/* ─── SSD1306 init ───────────────────────────────────────────────────────── */

esp_err_t oled_init(void)
{
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "i2c_bus_init() must be called before oled_init()");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OLED_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "dev_add");

    /* Standard SSD1306 128×64 init sequence */
    static const uint8_t init_seq[] = {
        0xAE,         /* Display OFF                              */
        0xD5, 0x80,   /* Clock divide / oscillator frequency      */
        0xA8, 0x3F,   /* Multiplex ratio = 63 (64 rows)           */
        0xD3, 0x00,   /* Display offset = 0                       */
        0x40,         /* Start line = 0                           */
        0x8D, 0x14,   /* Charge pump ON                           */
        0x20, 0x00,   /* Horizontal addressing mode               */
        0xA1,         /* Segment re-map: col 127 → SEG0           */
        0xC8,         /* COM scan direction: remapped             */
        0xDA, 0x12,   /* COM pins configuration                   */
        0x81, 0xCF,   /* Contrast                                 */
        0xD9, 0xF1,   /* Pre-charge period                        */
        0xDB, 0x40,   /* VCOMH deselect level                     */
        0xA4,         /* Display follows RAM                      */
        0xA6,         /* Normal (non-inverted) display            */
        0xAF,         /* Display ON                               */
    };
    ESP_RETURN_ON_ERROR(ssd_cmd(init_seq, sizeof(init_seq)), TAG, "init_seq");

    fb_clear();
    fb_flush();

    ESP_LOGI(TAG, "SSD1306 128×64 ready  I2C addr=0x%02X", OLED_ADDR);
    return ESP_OK;
}

/* ─── Display render ─────────────────────────────────────────────────────── */

static void oled_render(void)
{
    fb_clear();

    /* ── Snapshot state ─────────────────────────────────────────────── */
    float    temp     = 0.0f;
    float    hum      = 0.0f;
    float    fan_pct  = 0.0f;
    bool     auto_md  = true;
    uint32_t fan_rpm  = 0;

    if (xSemaphoreTake(g_state.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        temp    = g_state.temperature;
        hum     = g_state.humidity;
        fan_pct = g_state.fan_current_pct;
        auto_md = g_state.auto_mode;
        fan_rpm = g_state.fan_rpm;
        xSemaphoreGive(g_state.mutex);
    }

    char time_str[6] = "--:--";
    {
        char full[24];
        night_mode_get_time_str(full, sizeof(full));
        /* Extract HH:MM from "YYYY-MM-DDTHH:MM:SS" */
        if (full[0] != '1' || full[1] != '9') { /* not 1970 = time is set */
            time_str[0] = full[11];
            time_str[1] = full[12];
            time_str[2] = ':';
            time_str[3] = full[14];
            time_str[4] = full[15];
            time_str[5] = '\0';
        }
    }

    /* ── Page 0: status bar ─────────────────────────────────────────── */
    fb_str_s(0,   0, auto_md ? "AUTO" : "MANU");
    fb_str_s(86,  0, time_str);
    fb_hline(7); /* separator line at bottom of page 0 */

    /* ── Pages 1-2: temperature (large, 2× scale) ──────────────────── */
    char t_str[10];
    if (temp < 1.0f) {
        snprintf(t_str, sizeof(t_str), "  ---\x7F" "C");
    } else {
        snprintf(t_str, sizeof(t_str), " %5.1f\x7F" "C", temp);
    }
    /* Centre: 7 chars × 12 px = 84 px → start at (128-84)/2 = 22 */
    fb_str_l(10, 1, t_str);

    /* ── Page 3: humidity ────────────────────────────────────────────── */
    char h_str[20];
    snprintf(h_str, sizeof(h_str), "Feuchte: %4.0f%%", hum);
    fb_str_s(0, 3, h_str);

    /* ── Page 4: fan speed (text) ────────────────────────────────────── */
    char f_str[20];
    snprintf(f_str, sizeof(f_str), "Luefter: %3.0f%%", fan_pct);
    fb_str_s(0, 4, f_str);

    /* ── Page 5: RPM ─────────────────────────────────────────────────── */
    char r_str[20];
    snprintf(r_str, sizeof(r_str), "  %4u RPM", (unsigned)fan_rpm);
    fb_str_s(0, 5, r_str);

    /* ── Page 6: fan speed bar ──────────────────────────────────────── */
    fb_bar(6, 1, 6, (int)fan_pct);
}

/* ─── Task ───────────────────────────────────────────────────────────────── */

static void oled_task(void *pvParam)
{
    (void)pvParam;
    for (;;) {
        oled_render();
        if (fb_flush() != ESP_OK) {
            ESP_LOGW(TAG, "Display flush failed");
        }
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

void oled_start_task(void)
{
    xTaskCreate(oled_task, "oled", 4096, NULL, 3, NULL);
    ESP_LOGD(TAG, "OLED task started  refresh=2500 ms");
}

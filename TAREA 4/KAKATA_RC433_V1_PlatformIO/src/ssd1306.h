#pragma once

#include "driver/i2c_master.h"

#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64
#define SSD1306_PAGES       (SSD1306_HEIGHT / 8)
#define SSD1306_BUF_SIZE    (SSD1306_WIDTH * SSD1306_PAGES)

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t buffer[SSD1306_BUF_SIZE];
    uint8_t changed_pages[SSD1306_PAGES];
    bool initialized;
} ssd1306_handle_t;

void ssd1306_init(ssd1306_handle_t *handle, i2c_master_dev_handle_t dev);
void ssd1306_update(ssd1306_handle_t *handle);
void ssd1306_clear(ssd1306_handle_t *handle);
void ssd1306_set_pixel(ssd1306_handle_t *handle, int x, int y, bool on);
void ssd1306_draw_char(ssd1306_handle_t *handle, int x, int y, char c, bool invert);
void ssd1306_draw_string(ssd1306_handle_t *handle, int x, int y, const char *str, bool invert);
void ssd1306_draw_string_scaled(ssd1306_handle_t *handle, int x, int y, const char *str, int scale, bool invert);
void ssd1306_draw_hline(ssd1306_handle_t *handle, int x, int y, int w);
void ssd1306_draw_vline(ssd1306_handle_t *handle, int x, int y, int h);
void ssd1306_draw_rect(ssd1306_handle_t *handle, int x, int y, int w, int h);
void ssd1306_fill_rect(ssd1306_handle_t *handle, int x, int y, int w, int h);
void ssd1306_fill_rect_xor(ssd1306_handle_t *handle, int x, int y, int w, int h);
void ssd1306_draw_bar(ssd1306_handle_t *handle, int x, int y, int w, int h, int value, int max_value);
void ssd1306_draw_crosshair(ssd1306_handle_t *handle, int cx, int cy, int size, int dot_x, int dot_y);

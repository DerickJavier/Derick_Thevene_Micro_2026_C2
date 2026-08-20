#include "ssd1306.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SSD1306_CMD     0x00
#define SSD1306_DATA    0x40

static const uint8_t ssd1306_init_cmds[] = {
    0xAE,       // Display OFF
    0xD5, 0x80, // Set display clock divide ratio
    0xA8, 0x3F, // Set multiplex ratio (1 to 64)
    0xD3, 0x00, // Set display offset to 0
    0x40,       // Set start line address to 0
    0x8D, 0x14, // Enable charge pump
    0x20, 0x00, // Set memory addressing mode horizontal
    0xA1,       // Set segment re-map (column 127 mapped to SEG0)
    0xC8,       // Set COM output scan direction (remapped)
    0xDA, 0x12, // Set COM pins hardware configuration
    0x81, 0xCF, // Set contrast
    0xD9, 0xF1, // Set pre-charge period
    0xDB, 0x40, // Set VCOMH deselect level
    0xA4,       // Entire display ON (resume)
    0xA6,       // Normal display (not inverted)
    0xAF,       // Display ON
};

static void ssd1306_cmd(ssd1306_handle_t *handle, uint8_t cmd)
{
    uint8_t data[2] = {SSD1306_CMD, cmd};
    i2c_master_transmit(handle->i2c_dev, data, 2, pdMS_TO_TICKS(10));
}

static void ssd1306_cmd_list(ssd1306_handle_t *handle, const uint8_t *cmds, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ssd1306_cmd(handle, cmds[i]);
    }
}

void ssd1306_init(ssd1306_handle_t *handle, i2c_master_dev_handle_t dev)
{
    handle->i2c_dev = dev;
    handle->initialized = false;

    vTaskDelay(pdMS_TO_TICKS(100));

    ssd1306_cmd_list(handle, ssd1306_init_cmds, sizeof(ssd1306_init_cmds));

    ssd1306_clear(handle);
    ssd1306_update(handle);

    handle->initialized = true;
}

void ssd1306_update(ssd1306_handle_t *handle)
{
    ssd1306_cmd(handle, 0x21); // Column address range
    ssd1306_cmd(handle, 0);
    ssd1306_cmd(handle, SSD1306_WIDTH - 1);
    ssd1306_cmd(handle, 0x22); // Page address range
    ssd1306_cmd(handle, 0);
    ssd1306_cmd(handle, SSD1306_PAGES - 1);

    for (int page = 0; page < SSD1306_PAGES; page++) {
        if (!handle->changed_pages[page]) continue;

        uint8_t packet[SSD1306_WIDTH + 1];
        packet[0] = SSD1306_DATA;
        memcpy(&packet[1], &handle->buffer[page * SSD1306_WIDTH], SSD1306_WIDTH);
        i2c_master_transmit(handle->i2c_dev, packet, sizeof(packet), pdMS_TO_TICKS(10));
        handle->changed_pages[page] = 0;
    }
}

void ssd1306_clear(ssd1306_handle_t *handle)
{
    memset(handle->buffer, 0, SSD1306_BUF_SIZE);
    for (int i = 0; i < SSD1306_PAGES; i++) {
        handle->changed_pages[i] = 1;
    }
}

void ssd1306_set_pixel(ssd1306_handle_t *handle, int x, int y, bool on)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) return;

    int page = y / 8;
    int bit = y % 8;
    int idx = page * SSD1306_WIDTH + x;

    if (on) {
        handle->buffer[idx] |= (1 << bit);
    } else {
        handle->buffer[idx] &= ~(1 << bit);
    }
    handle->changed_pages[page] = 1;
}

void ssd1306_fill_rect(ssd1306_handle_t *handle, int x, int y, int w, int h)
{
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            ssd1306_set_pixel(handle, x + dx, y + dy, true);
        }
    }
}

void ssd1306_fill_rect_xor(ssd1306_handle_t *handle, int x, int y, int w, int h)
{
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px < 0 || px >= SSD1306_WIDTH || py < 0 || py >= SSD1306_HEIGHT) continue;
            int page = py / 8;
            int bit = py % 8;
            int idx = page * SSD1306_WIDTH + px;
            handle->buffer[idx] ^= (1 << bit);
            handle->changed_pages[page] = 1;
        }
    }
}

void ssd1306_draw_hline(ssd1306_handle_t *handle, int x, int y, int w)
{
    for (int i = 0; i < w; i++) {
        ssd1306_set_pixel(handle, x + i, y, true);
    }
}

void ssd1306_draw_vline(ssd1306_handle_t *handle, int x, int y, int h)
{
    for (int i = 0; i < h; i++) {
        ssd1306_set_pixel(handle, x, y + i, true);
    }
}

void ssd1306_draw_rect(ssd1306_handle_t *handle, int x, int y, int w, int h)
{
    ssd1306_draw_hline(handle, x, y, w);
    ssd1306_draw_hline(handle, x, y + h - 1, w);
    ssd1306_draw_vline(handle, x, y, h);
    ssd1306_draw_vline(handle, x + w - 1, y, h);
}

void ssd1306_draw_bar(ssd1306_handle_t *handle, int x, int y, int w, int h, int value, int max_value)
{
    ssd1306_draw_rect(handle, x, y, w, h);
    if (max_value == 0) return;

    int bar_w = (w - 2) * abs(value) / max_value;
    if (bar_w > w - 2) bar_w = w - 2;

    int bar_x;
    if (value >= 0) {
        bar_x = x + 1;
    } else {
        bar_x = x + 1 + (w - 2) - bar_w;
    }

    if (bar_w > 0) {
        ssd1306_fill_rect(handle, bar_x, y + 1, bar_w, h - 2);
    }

    int center_x = x + w / 2;
    ssd1306_draw_vline(handle, center_x, y + 1, h - 2);
}

void ssd1306_draw_crosshair(ssd1306_handle_t *handle, int cx, int cy, int size, int dot_x, int dot_y)
{
    int half = size / 2;
    int left = cx - half;
    int top = cy - half;

    ssd1306_draw_rect(handle, left, top, size, size);
    ssd1306_draw_hline(handle, left, cy, size);
    ssd1306_draw_vline(handle, cx, top, size);

    int dx = left + half + (dot_x * half) / 100;
    int dy = cy - (dot_y * half) / 100;

    if (dx < left) dx = left;
    if (dx >= left + size) dx = left + size - 1;
    if (dy < top) dy = top;
    if (dy >= top + size) dy = top + size - 1;

    ssd1306_fill_rect(handle, dx - 1, dy - 1, 3, 3);
}

static const uint8_t font_5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // space
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x00, 0x08, 0x14, 0x22, 0x41, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x01, 0x01, // F
    0x3E, 0x41, 0x41, 0x51, 0x32, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x04, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x03, 0x04, 0x78, 0x04, 0x03, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x00, 0x7F, 0x41, 0x41, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // backslash
    0x41, 0x41, 0x7F, 0x00, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, // a
    0x7F, 0x48, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x20, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, // d
    0x38, 0x54, 0x54, 0x54, 0x18, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, // f
    0x08, 0x14, 0x54, 0x54, 0x3C, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x00, 0x7F, 0x10, 0x28, 0x44, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, // r
    0x48, 0x54, 0x54, 0x54, 0x20, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08, // ~
};

void ssd1306_draw_char(ssd1306_handle_t *handle, int x, int y, char c, bool invert)
{
    if (c < 0x20 || c > 0x7E) c = ' ';
    int idx = (c - 0x20) * 5;

    for (int col = 0; col < 5; col++) {
        uint8_t line = font_5x7[idx + col];
        for (int row = 0; row < 8; row++) {
            bool on = (line >> row) & 1;
            if (invert) on = !on;
            ssd1306_set_pixel(handle, x + col, y + row, on);
        }
    }
}

void ssd1306_draw_string(ssd1306_handle_t *handle, int x, int y, const char *str, bool invert)
{
    while (*str) {
        ssd1306_draw_char(handle, x, y, *str, invert);
        x += 6;
        str++;
    }
}

void ssd1306_draw_string_scaled(ssd1306_handle_t *handle, int x, int y, const char *str, int scale, bool invert)
{
    while (*str) {
        ssd1306_draw_char(handle, x, y, *str, invert);
        x += 6 * scale;
        str++;
    }
}

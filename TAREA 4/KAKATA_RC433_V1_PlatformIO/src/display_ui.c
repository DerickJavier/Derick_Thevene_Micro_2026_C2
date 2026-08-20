#include "display_ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "DISPLAY"

static char line_buf[32];

void display_ui_init(ssd1306_handle_t *disp)
{
    ssd1306_clear(disp);
    ssd1306_update(disp);
}

static void draw_status_bar(ssd1306_handle_t *disp, const display_status_t *status, float battery)
{
    ssd1306_draw_string(disp, 0, 0, "W", !status->wifi_connected);
    ssd1306_draw_string(disp, 8, 0, "M", !status->mqtt_connected);

    int bat_pct = (int)((battery - 3.0f) * 100.0f / 1.2f);
    if (bat_pct < 0) bat_pct = 0;
    if (bat_pct > 100) bat_pct = 100;

    ssd1306_draw_rect(disp, 90, 0, 34, 9);
    ssd1306_fill_rect(disp, 124, 2, 3, 5);
    int bar_w = 30 * bat_pct / 100;
    if (bar_w > 0) {
        ssd1306_fill_rect(disp, 92, 2, bar_w, 5);
    }

    snprintf(line_buf, sizeof(line_buf), "%d%%", bat_pct);
    ssd1306_draw_string(disp, 100, 0, line_buf, false);
}

static void draw_joy_crosshair(ssd1306_handle_t *disp, int cx, int cy, int size,
                                int8_t x_val, int8_t y_val, const char *label)
{
    ssd1306_draw_crosshair(disp, cx, cy, size, x_val, y_val);

    snprintf(line_buf, sizeof(line_buf), "%s%+4d", label, x_val);
    ssd1306_draw_string(disp, cx - size / 2, cy + size / 2 + 1, line_buf, false);

    snprintf(line_buf, sizeof(line_buf), "Y%+4d", y_val);
    ssd1306_draw_string(disp, cx + size / 2 + 2, cy - 3, line_buf, false);
}

static void draw_sensor_bars(ssd1306_handle_t *disp, int y, const char *label,
                              float x_val, float y_val, float z_val)
{
    snprintf(line_buf, sizeof(line_buf), "%s", label);
    ssd1306_draw_string(disp, 0, y, line_buf, true);

    snprintf(line_buf, sizeof(line_buf), "X%+6.1f", x_val);
    ssd1306_draw_string(disp, 30, y, line_buf, false);

    snprintf(line_buf, sizeof(line_buf), "Y%+6.1f", y_val);
    ssd1306_draw_string(disp, 72, y, line_buf, false);

    snprintf(line_buf, sizeof(line_buf), "Z%+6.1f", z_val);
    ssd1306_draw_string(disp, 0, y + 8, line_buf, false);
}

static void draw_button_states(ssd1306_handle_t *disp, int y, uint32_t btn_state)
{
    ssd1306_draw_string(disp, 0, y, "B:", false);

    for (int i = 0; i < 10; i++) {
        bool pressed = (btn_state >> i) & 1;
        int bx = 14 + i * 11;
        if (bx + 8 > SSD1306_WIDTH) break;
        if (pressed) {
            ssd1306_fill_rect(disp, bx, y, 9, 6);
        } else {
            ssd1306_draw_rect(disp, bx, y, 9, 6);
        }
    }
}

void display_ui_update(ssd1306_handle_t *disp, const input_data_t *input,
                       const mpu6050_data_t *sensor, const display_status_t *status)
{
    ssd1306_clear(disp);

    draw_status_bar(disp, status, input->battery_voltage);

    draw_joy_crosshair(disp, 24, 20, 16, input->joy1_x, input->joy1_y, "L");
    draw_joy_crosshair(disp, 88, 20, 16, input->joy2_x, input->joy2_y, "R");

    draw_sensor_bars(disp, 36, "G", sensor->gyro_x, sensor->gyro_y, sensor->gyro_z);

    snprintf(line_buf, sizeof(line_buf), "A%+5.2f %+5.2f %+5.2f",
             sensor->accel_x, sensor->accel_y, sensor->accel_z);
    ssd1306_draw_string(disp, 0, 52, line_buf, false);

    draw_button_states(disp, 60, input->button_state);

    ssd1306_update(disp);
}

void display_ui_calibration_screen(ssd1306_handle_t *disp, int countdown)
{
    ssd1306_clear(disp);

    ssd1306_draw_string(disp, 16, 8, "CALIBRACION", true);
    ssd1306_draw_string(disp, 24, 20, "Mantener", false);
    ssd1306_draw_string(disp, 20, 30, "botones...", false);

    int bar_w = 80 * countdown / 3000;
    ssd1306_draw_rect(disp, 24, 44, 82, 10);
    if (bar_w > 0) {
        ssd1306_fill_rect(disp, 25, 45, bar_w, 8);
    }

    snprintf(line_buf, sizeof(line_buf), "%d.%ds", countdown / 1000, (countdown % 1000) / 100);
    ssd1306_draw_string(disp, 52, 56, line_buf, false);

    ssd1306_update(disp);
}

void display_ui_splash(ssd1306_handle_t *disp)
{
    ssd1306_clear(disp);

    ssd1306_draw_string(disp, 20, 16, "KAKATA", true);
    ssd1306_draw_string(disp, 12, 28, "RC-433 V1", false);
    ssd1306_draw_string(disp, 8, 44, "ITLA-HUB 2025", false);

    ssd1306_update(disp);
    vTaskDelay(pdMS_TO_TICKS(1500));
}

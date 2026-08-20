#pragma once

#include "ssd1306.h"
#include "input.h"
#include "mpu6050.h"

typedef struct {
    bool wifi_connected;
    bool mqtt_connected;
    bool calibrating;
    bool calibrated;
} display_status_t;

void display_ui_init(ssd1306_handle_t *disp);
void display_ui_update(ssd1306_handle_t *disp, const input_data_t *input,
                       const mpu6050_data_t *sensor, const display_status_t *status);
void display_ui_calibration_screen(ssd1306_handle_t *disp, int countdown);
void display_ui_splash(ssd1306_handle_t *disp);

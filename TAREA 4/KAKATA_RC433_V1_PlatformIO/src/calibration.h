#pragma once

#include "pin_config.h"

typedef struct {
    joystick_offsets_t joy_offsets[NUM_JOYSTICKS];
    bool calibrated;
} calibration_data_t;

void calibration_init(void);
void calibration_save(const calibration_data_t *data);
bool calibration_load(calibration_data_t *data);
void calibration_set_zero(const int16_t joy1_x, const int16_t joy1_y,
                          const int16_t joy2_x, const int16_t joy2_y,
                          calibration_data_t *data);
int16_t calibration_apply_offset(int16_t raw, int16_t offset);

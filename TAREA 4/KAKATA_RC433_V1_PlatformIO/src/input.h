#pragma once

#include "esp_adc/adc_oneshot.h"
#include "pin_config.h"

#define NUM_JOYSTICKS       2
#define NUM_AXIS_PER_JOY    2
#define NUM_FACE_BUTTONS    4
#define NUM_LATERAL_BUTTONS 4
#define NUM_TOTAL_BUTTONS   10

typedef struct {
    int8_t joy1_x;
    int8_t joy1_y;
    int8_t joy2_x;
    int8_t joy2_y;
    uint32_t button_state;
    float battery_voltage;
} input_data_t;

void input_init(adc_oneshot_unit_handle_t *adc_handle);
void input_read_all(adc_oneshot_unit_handle_t adc, input_data_t *data);
int8_t input_map_joystick(int raw_value);
bool input_read_button(gpio_num_t pin);
uint32_t input_read_all_buttons(void);
float input_read_battery(adc_oneshot_unit_handle_t adc);

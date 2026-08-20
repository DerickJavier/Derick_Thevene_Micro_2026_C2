#include "input.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdlib.h>

#define TAG "INPUT"

static const gpio_num_t face_btn_pins[NUM_FACE_BUTTONS] = {
    PIN_BTN_0, PIN_BTN_1, PIN_BTN_2, PIN_BTN_3
};

static const gpio_num_t lateral_btn_pins[NUM_LATERAL_BUTTONS] = {
    PIN_BTN_L1, PIN_BTN_L2, PIN_BTN_L3, PIN_BTN_L4
};

static const gpio_num_t joy_btn_pins[NUM_JOYSTICKS] = {
    PIN_JOY0_BTN, PIN_JOY1_BTN
};

static const adc_channel_t joy_adc_channels[NUM_JOYSTICKS][NUM_AXIS_PER_JOY] = {
    {ADC_JOY0_X_CH, ADC_JOY0_Y_CH},
    {ADC_JOY1_X_CH, ADC_JOY1_Y_CH},
};

void input_init(adc_oneshot_unit_handle_t *adc_handle)
{
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .attenuation = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(*adc_handle, ADC_JOY0_X_CH, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(*adc_handle, ADC_JOY0_Y_CH, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(*adc_handle, ADC_JOY1_X_CH, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(*adc_handle, ADC_JOY1_Y_CH, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(*adc_handle, ADC_VBAT_CH, &chan_cfg));

    gpio_config_t io_cfg = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    io_cfg.pin_bit_mask = (1ULL << PIN_JOY0_BTN) | (1ULL << PIN_JOY1_BTN);
    gpio_config(&io_cfg);

    for (int i = 0; i < NUM_FACE_BUTTONS; i++) {
        io_cfg.pin_bit_mask = (1ULL << face_btn_pins[i]);
        gpio_config(&io_cfg);
    }

    for (int i = 0; i < NUM_LATERAL_BUTTONS; i++) {
        io_cfg.pin_bit_mask = (1ULL << lateral_btn_pins[i]);
        gpio_config(&io_cfg);
    }

    ESP_LOGI(TAG, "Input initialized: 4 ADC channels, %d digital buttons",
             NUM_FACE_BUTTONS + NUM_LATERAL_BUTTONS + NUM_JOYSTICKS);
}

int8_t input_map_joystick(int raw_value)
{
    int32_t centered = (int32_t)raw_value - ADC_CENTER_VALUE;
    int32_t mapped = (centered * JOY_MAX) / ADC_CENTER_VALUE;

    if (mapped > JOY_MAX) mapped = JOY_MAX;
    if (mapped < JOY_MIN) mapped = JOY_MIN;

    if (abs(mapped) < JOY_DEADZONE) mapped = 0;

    return (int8_t)mapped;
}

bool input_read_button(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

uint32_t input_read_all_buttons(void)
{
    uint32_t state = 0;

    for (int i = 0; i < NUM_FACE_BUTTONS; i++) {
        if (input_read_button(face_btn_pins[i])) {
            state |= (1 << i);
        }
    }

    for (int i = 0; i < NUM_LATERAL_BUTTONS; i++) {
        if (input_read_button(lateral_btn_pins[i])) {
            state |= (1 << (NUM_FACE_BUTTONS + i));
        }
    }

    for (int i = 0; i < NUM_JOYSTICKS; i++) {
        if (input_read_button(joy_btn_pins[i])) {
            state |= (1 << (NUM_FACE_BUTTONS + NUM_LATERAL_BUTTONS + i));
        }
    }

    return state;
}

float input_read_battery(adc_oneshot_unit_handle_t adc)
{
    int raw;
    adc_oneshot_read(adc, ADC_VBAT_CH, &raw);
    float voltage = (float)raw * 3.3f * 2.0f / 4095.0f;
    return voltage;
}

void input_read_all(adc_oneshot_unit_handle_t adc, input_data_t *data)
{
    int raw;

    adc_oneshot_read(adc, ADC_JOY0_X_CH, &raw);
    data->joy1_x = input_map_joystick(raw);

    adc_oneshot_read(adc, ADC_JOY0_Y_CH, &raw);
    data->joy1_y = input_map_joystick(raw);

    adc_oneshot_read(adc, ADC_JOY1_X_CH, &raw);
    data->joy2_x = input_map_joystick(raw);

    adc_oneshot_read(adc, ADC_JOY1_Y_CH, &raw);
    data->joy2_y = input_map_joystick(raw);

    data->button_state = input_read_all_buttons();

    data->battery_voltage = input_read_battery(adc);
}

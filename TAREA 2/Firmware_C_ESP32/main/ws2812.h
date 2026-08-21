#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

esp_err_t ws2812_init(gpio_num_t gpio);
esp_err_t ws2812_write(rgb_t color);

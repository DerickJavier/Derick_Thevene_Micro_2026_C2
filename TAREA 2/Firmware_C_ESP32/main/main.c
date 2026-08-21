#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"

#include "ws2812.h"
#include "ens210.h"

#define PIN_I2C_SDA    GPIO_NUM_18
#define PIN_I2C_SCL    GPIO_NUM_17
#define PIN_WS2812     GPIO_NUM_48
#define PIN_LED_STATUS GPIO_NUM_33
#define PIN_BTN_BOOT   GPIO_NUM_0

#define SAMPLE_PERIOD_MS 2000

static const char *TAG = "tarea2";

typedef enum {
    MODE_TEMP_COLOR = 0,
    MODE_DEMO,
} app_mode_t;

static i2c_master_bus_handle_t s_i2c_bus;

static void i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));
}

static void set_temp_color(float t)
{
    rgb_t c = { 0, 0, 0 };

    if (t < 18.0f) {
        c.b = 255;
    } else if (t < 26.0f) {
        c.g = 255;
    } else if (t < 32.0f) {
        c.r = 255;
        c.g = 120;
    } else {
        c.r = 255;
    }

    ws2812_write(c);
}

static void demo_color(void)
{
    static uint8_t step = 0;
    rgb_t palette[3] = {
        { 255, 0, 0 },
        { 0, 255, 0 },
        { 0, 0, 255 },
    };

    ws2812_write(palette[step]);
    step = (step + 1) % 3;
}

void app_main(void)
{
    gpio_reset_pin(PIN_LED_STATUS);
    gpio_set_direction(PIN_LED_STATUS, GPIO_MODE_OUTPUT);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << PIN_BTN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_cfg);

    ESP_ERROR_CHECK(ws2812_init(PIN_WS2812));
    i2c_bus_init();
    ESP_ERROR_CHECK(ens210_init(s_i2c_bus));

    ESP_LOGI(TAG, "ESP32-S3 listo: WS2812B=GPIO48 ENS210=SDA%d/SCL%d LED=GPIO%d",
             PIN_I2C_SDA, PIN_I2C_SCL, PIN_LED_STATUS);

    app_mode_t mode = MODE_TEMP_COLOR;
    int last_btn = 1;
    bool led_on = false;

    while (1) {
        int btn = gpio_get_level(PIN_BTN_BOOT);
        if (last_btn == 1 && btn == 0) {
            mode = (mode == MODE_TEMP_COLOR) ? MODE_DEMO : MODE_TEMP_COLOR;
            ESP_LOGI(TAG, "modo: %s", (mode == MODE_TEMP_COLOR) ? "indicador de temperatura" : "demo");
        }
        last_btn = btn;

        float t = 0.0f;
        float h = 0.0f;
        esp_err_t err = ens210_read(&t, &h);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "T=%.2f C  HR=%.2f %%", t, h);
            if (mode == MODE_TEMP_COLOR) {
                set_temp_color(t);
            } else {
                demo_color();
            }
        } else {
            ESP_LOGE(TAG, "error leyendo ENS210: %s", esp_err_to_name(err));
            rgb_t magenta = { 180, 0, 180 };
            ws2812_write(magenta);
        }

        led_on = !led_on;
        gpio_set_level(PIN_LED_STATUS, led_on);

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

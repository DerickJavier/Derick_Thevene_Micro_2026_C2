#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "pin_config.h"
#include "ssd1306.h"
#include "mpu6050.h"
#include "input.h"
#include "display_ui.h"
#include "mqtt_app.h"
#include "calibration.h"

#define TAG "MAIN"

#define WIFI_SSID           "TU_WIFI_SSID"
#define WIFI_PASSWORD       "TU_WIFI_PASSWORD"
#define MQTT_BROKER_IP      "192.168.1.100"
#define MQTT_BROKER_PORT    1883

#define CAL_HOLD_MS         3000
#define JOY0_BTN_BIT        (1 << 8)
#define JOY1_BTN_BIT        (1 << 9)

typedef struct {
    input_data_t input;
    mpu6050_data_t sensor;
    display_status_t status;
    calibration_data_t cal;
    SemaphoreHandle_t mutex;
    bool cal_active;
    int cal_countdown;
} app_data_t;

static app_data_t app_data;
static i2c_master_bus_handle_t i2c_bus;

static void i2c_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));
    ESP_LOGI(TAG, "I2C bus initialized on SDA=%d SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);
}

static void led_init(void)
{
    gpio_config_t io_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << PIN_LED_1) | (1ULL << PIN_LED_2) | (1ULL << PIN_LED_3) |
                        (1ULL << PIN_LED_4) | (1ULL << PIN_LED_5) | (1ULL << PIN_LED_6),
    };
    gpio_config(&io_cfg);
}

static int8_t apply_cal_and_map(int raw, int16_t cal_zero)
{
    int centered = raw - cal_zero;
    int mapped = (centered * JOY_MAX) / ADC_CENTER_VALUE;
    if (mapped > JOY_MAX) mapped = JOY_MAX;
    if (mapped < JOY_MIN) mapped = JOY_MIN;
    if (abs(mapped) < JOY_DEADZONE) mapped = 0;
    return (int8_t)mapped;
}

static void input_task(void *arg)
{
    app_data_t *data = (app_data_t *)arg;
    adc_oneshot_unit_handle_t adc_handle;
    input_init(&adc_handle);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t hold_start = 0;
    bool both_prev = false;

    while (1) {
        input_data_t inp;
        inp.button_state = input_read_all_buttons();
        inp.battery_voltage = input_read_battery(adc_handle);

        int raw[4];
        adc_oneshot_read(adc_handle, ADC_JOY0_X_CH, &raw[0]);
        adc_oneshot_read(adc_handle, ADC_JOY0_Y_CH, &raw[1]);
        adc_oneshot_read(adc_handle, ADC_JOY1_X_CH, &raw[2]);
        adc_oneshot_read(adc_handle, ADC_JOY1_Y_CH, &raw[3]);

        bool j0 = (inp.button_state & JOY0_BTN_BIT) != 0;
        bool j1 = (inp.button_state & JOY1_BTN_BIT) != 0;
        bool both = j0 && j1;

        if (both) {
            if (!both_prev) {
                hold_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
            }
            uint32_t held = (xTaskGetTickCount() * portTICK_PERIOD_MS) - hold_start;
            int countdown = (int)(CAL_HOLD_MS - held);
            if (countdown < 0) countdown = 0;

            if (held >= CAL_HOLD_MS) {
                if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    calibration_set_zero(raw[0], raw[1], raw[2], raw[3], &data->cal);
                    data->cal_active = false;
                    data->cal_countdown = 0;
                    xSemaphoreGive(data->mutex);
                }
                ESP_LOGI(TAG, "Calibration saved!");
            } else {
                if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    data->cal_active = true;
                    data->cal_countdown = countdown;
                    xSemaphoreGive(data->mutex);
                }
            }
        } else {
            if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                data->cal_active = false;
                data->cal_countdown = 0;
                xSemaphoreGive(data->mutex);
            }
        }
        both_prev = both;

        if (data->cal.calibrated) {
            inp.joy1_x = apply_cal_and_map(raw[0], data->cal.joy_offsets[0].raw_x);
            inp.joy1_y = apply_cal_and_map(raw[1], data->cal.joy_offsets[0].raw_y);
            inp.joy2_x = apply_cal_and_map(raw[2], data->cal.joy_offsets[1].raw_x);
            inp.joy2_y = apply_cal_and_map(raw[3], data->cal.joy_offsets[1].raw_y);
        } else {
            inp.joy1_x = apply_cal_and_map(raw[0], 0);
            inp.joy1_y = apply_cal_and_map(raw[1], 0);
            inp.joy2_x = apply_cal_and_map(raw[2], 0);
            inp.joy2_y = apply_cal_and_map(raw[3], 0);
        }

        if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            data->input = inp;
            xSemaphoreGive(data->mutex);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));
    }
}

static void sensor_task(void *arg)
{
    app_data_t *data = (app_data_t *)arg;

    i2c_device_config_t mpu_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6050_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t mpu_dev;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &mpu_cfg, &mpu_dev));

    mpu6050_init(mpu_dev);

    mpu6050_offsets_t offsets;
    memset(&offsets, 0, sizeof(offsets));
    mpu6050_calibrate(mpu_dev, &offsets, 500);

    TickType_t last_wake = xTaskGetTickCount();
    mpu6050_data_t sdata;

    while (1) {
        mpu6050_read_all(mpu_dev, &sdata);
        mpu6050_apply_offsets(&sdata, &offsets);

        if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            data->sensor = sdata;
            xSemaphoreGive(data->mutex);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

static void display_task(void *arg)
{
    app_data_t *data = (app_data_t *)arg;

    i2c_device_config_t ssd_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_I2C_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t disp_dev;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &ssd_cfg, &disp_dev));

    ssd1306_handle_t display;
    ssd1306_init(&display, disp_dev);
    display_ui_init(&display);
    display_ui_splash(&display);

    while (1) {
        input_data_t inp_cpy;
        mpu6050_data_t sen_cpy;
        display_status_t st = {0};
        bool cal_active = false;
        int cal_cd = 0;

        if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            inp_cpy = data->input;
            sen_cpy = data->sensor;
            cal_active = data->cal_active;
            cal_cd = data->cal_countdown;
            st.calibrated = data->cal.calibrated;
            xSemaphoreGive(data->mutex);
        }

        mqtt_status_t mst = mqtt_app_get_status();
        st.wifi_connected = mst.wifi_connected;
        st.mqtt_connected = mst.mqtt_connected;

        if (cal_active) {
            display_ui_calibration_screen(&display, cal_cd);
        } else {
            display_ui_update(&display, &inp_cpy, &sen_cpy, &st);
        }

        gpio_set_level(PIN_LED_1, mst.wifi_connected);
        gpio_set_level(PIN_LED_2, mst.mqtt_connected);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void mqtt_task(void *arg)
{
    app_data_t *data = (app_data_t *)arg;

    mqtt_app_init();
    mqtt_app_start_wifi(WIFI_SSID, WIFI_PASSWORD);
    vTaskDelay(pdMS_TO_TICKS(3000));
    mqtt_app_start_mqtt(MQTT_BROKER_IP, MQTT_BROKER_PORT);

    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        input_data_t inp_cpy;
        mpu6050_data_t sen_cpy;

        if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            inp_cpy = data->input;
            sen_cpy = data->sensor;
            xSemaphoreGive(data->mutex);
        }

        mqtt_app_publish_data(&inp_cpy, &sen_cpy);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== KAKATA RC-433 V1 Firmware ===");

    memset(&app_data, 0, sizeof(app_data));
    app_data.mutex = xSemaphoreCreateMutex();

    i2c_init();
    led_init();
    calibration_init();

    if (calibration_load(&app_data.cal)) {
        ESP_LOGI(TAG, "Calibration loaded from NVS");
    } else {
        ESP_LOGW(TAG, "No calibration data, using factory defaults");
    }

    xTaskCreatePinnedToCore(input_task, "input", 4096, &app_data, 5, NULL, 0);
    xTaskCreatePinnedToCore(sensor_task, "sensor", 4096, &app_data, 4, NULL, 0);
    xTaskCreatePinnedToCore(display_task, "display", 8192, &app_data, 3, NULL, 1);
    xTaskCreatePinnedToCore(mqtt_task, "mqtt", 8192, &app_data, 2, NULL, 1);

    ESP_LOGI(TAG, "All tasks created");
    gpio_set_level(PIN_LED_3, 1);
}

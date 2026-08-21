#include "ens210.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ENS210_I2C_ADDR       0x43
#define ENS210_REG_T_VAL      0x20
#define ENS210_REG_H_VAL      0x22
#define ENS210_MEAS_TIME_MS   5
#define ENS210_I2C_TIMEOUT_MS 50

static i2c_master_dev_handle_t s_dev;

esp_err_t ens210_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ENS210_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    return i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
}

static esp_err_t ens210_measure(uint8_t reg, uint16_t *raw)
{
    uint8_t trigger[2] = { reg, 0x01 };
    uint8_t data[3] = { 0 };

    esp_err_t err = i2c_master_transmit(s_dev, trigger, sizeof(trigger), ENS210_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(ENS210_MEAS_TIME_MS));

    err = i2c_master_transmit_receive(s_dev, &reg, 1, data, sizeof(data), ENS210_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *raw = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
    return ESP_OK;
}

esp_err_t ens210_read(float *temp_c, float *hum_pct)
{
    uint16_t t_raw = 0;
    uint16_t h_raw = 0;

    esp_err_t err = ens210_measure(ENS210_REG_T_VAL, &t_raw);
    if (err == ESP_OK) {
        err = ens210_measure(ENS210_REG_H_VAL, &h_raw);
    }
    if (err != ESP_OK) {
        return err;
    }

    float t = (float)t_raw / 64.0f - 273.15f;
    float h = (float)h_raw / 512.0f;

    if (t < -40.0f || t > 125.0f || h < 0.0f || h > 100.0f) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *temp_c = t;
    *hum_pct = h;
    return ESP_OK;
}

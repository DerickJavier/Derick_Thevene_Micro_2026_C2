#include "mpu6050.h"
#include "pin_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define MPU6050_REG_SMPLRT_DIV   0x19
#define MPU6050_REG_CONFIG       0x1A
#define MPU6050_REG_GYRO_CFG     0x1B
#define MPU6050_REG_ACCEL_CFG    0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_TEMP_OUT_H   0x41
#define MPU6050_REG_GYRO_XOUT_H  0x43
#define MPU6050_REG_PWR_MGMT_1   0x6B
#define MPU6050_REG_WHO_AM_I     0x75

static void mpu6050_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t data[2] = {reg, val};
    i2c_master_transmit(dev, data, 2, pdMS_TO_TICKS(100));
}

static uint8_t mpu6050_read_reg(i2c_master_dev_handle_t dev, uint8_t reg)
{
    uint8_t val;
    i2c_master_transmit_receive(dev, &reg, 1, &val, 1, pdMS_TO_TICKS(100));
    return val;
}

static void mpu6050_read_bytes(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_master_transmit_receive(dev, &reg, 1, buf, len, pdMS_TO_TICKS(100));
}

void mpu6050_init(i2c_master_dev_handle_t dev)
{
    uint8_t who = mpu6050_read_reg(dev, MPU6050_REG_WHO_AM_I);
    if (who != 0x68) {
        ESP_LOGE("MPU6050", "WHO_AM_I mismatch: 0x%02X (expected 0x68)", who);
        return;
    }

    mpu6050_write_reg(dev, MPU6050_REG_PWR_MGMT_1, 0x80);
    vTaskDelay(pdMS_TO_TICKS(100));

    mpu6050_write_reg(dev, MPU6050_REG_PWR_MGMT_1, 0x01);
    vTaskDelay(pdMS_TO_TICKS(10));

    mpu6050_write_reg(dev, MPU6050_REG_SMPLRT_DIV, 0x07);
    mpu6050_write_reg(dev, MPU6050_REG_CONFIG, 0x03);
    mpu6050_write_reg(dev, MPU6050_REG_GYRO_CFG, 0x00);
    mpu6050_write_reg(dev, MPU6050_REG_ACCEL_CFG, 0x00);

    ESP_LOGI("MPU6050", "Initialized successfully");
}

void mpu6050_read_all(i2c_master_dev_handle_t dev, mpu6050_data_t *data)
{
    uint8_t buf[14];
    mpu6050_read_bytes(dev, MPU6050_REG_ACCEL_XOUT_H, buf, 14);

    int16_t raw_ax = (buf[0] << 8) | buf[1];
    int16_t raw_ay = (buf[2] << 8) | buf[3];
    int16_t raw_az = (buf[4] << 8) | buf[5];
    int16_t raw_temp = (buf[6] << 8) | buf[7];
    int16_t raw_gx = (buf[8] << 8) | buf[9];
    int16_t raw_gy = (buf[10] << 8) | buf[11];
    int16_t raw_gz = (buf[12] << 8) | buf[13];

    data->accel_x = (float)raw_ax / 16384.0f;
    data->accel_y = (float)raw_ay / 16384.0f;
    data->accel_z = (float)raw_az / 16384.0f;

    data->gyro_x = (float)raw_gx / 131.0f;
    data->gyro_y = (float)raw_gy / 131.0f;
    data->gyro_z = (float)raw_gz / 131.0f;

    data->temperature = (float)raw_temp / 340.0f + 36.53f;
}

void mpu6050_apply_offsets(mpu6050_data_t *data, const mpu6050_offsets_t *offsets)
{
    data->gyro_x -= offsets->gyro_offset_x;
    data->gyro_y -= offsets->gyro_offset_y;
    data->gyro_z -= offsets->gyro_offset_z;
    data->accel_x -= offsets->accel_offset_x;
    data->accel_y -= offsets->accel_offset_y;
    data->accel_z -= offsets->accel_offset_z;
}

void mpu6050_calibrate(i2c_master_dev_handle_t dev, mpu6050_offsets_t *offsets, int samples)
{
    ESP_LOGI("MPU6050", "Calibrating with %d samples...", samples);

    float sum_gx = 0, sum_gy = 0, sum_gz = 0;
    float sum_ax = 0, sum_ay = 0, sum_az = 0;
    mpu6050_data_t data;

    for (int i = 0; i < samples; i++) {
        mpu6050_read_all(dev, &data);
        sum_gx += data.gyro_x;
        sum_gy += data.gyro_y;
        sum_gz += data.gyro_z;
        sum_ax += data.accel_x;
        sum_ay += data.accel_y;
        sum_az += data.accel_z;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    offsets->gyro_offset_x = sum_gx / samples;
    offsets->gyro_offset_y = sum_gy / samples;
    offsets->gyro_offset_z = sum_gz / samples;
    offsets->accel_offset_x = sum_ax / samples;
    offsets->accel_offset_y = sum_ay / samples;
    offsets->accel_offset_z = (sum_az / samples) - 1.0f;

    ESP_LOGI("MPU6050", "Calibration done. Gyro offsets: %.2f %.2f %.2f",
             offsets->gyro_offset_x, offsets->gyro_offset_y, offsets->gyro_offset_z);
}

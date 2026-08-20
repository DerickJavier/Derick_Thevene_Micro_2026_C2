#pragma once

#include "driver/i2c_master.h"

typedef struct {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    float temperature;
} mpu6050_data_t;

typedef struct {
    float gyro_offset_x;
    float gyro_offset_y;
    float gyro_offset_z;
    float accel_offset_x;
    float accel_offset_y;
    float accel_offset_z;
} mpu6050_offsets_t;

void mpu6050_init(i2c_master_dev_handle_t dev);
void mpu6050_read_all(i2c_master_dev_handle_t dev, mpu6050_data_t *data);
void mpu6050_apply_offsets(mpu6050_data_t *data, const mpu6050_offsets_t *offsets);
void mpu6050_calibrate(i2c_master_dev_handle_t dev, mpu6050_offsets_t *offsets, int samples);

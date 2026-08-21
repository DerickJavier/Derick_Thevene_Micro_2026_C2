#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t ens210_init(i2c_master_bus_handle_t bus);
esp_err_t ens210_read(float *temp_c, float *hum_pct);

#include "calibration.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

#define TAG "CAL"
#define NVS_NAMESPACE "calibration"
#define NVS_KEY "joy_offsets"

static nvs_handle_t nvs_handle;
static bool nvs_initialized = false;

void calibration_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return;
    }
    nvs_initialized = true;
    ESP_LOGI(TAG, "NVS initialized for calibration storage");
}

void calibration_save(const calibration_data_t *data)
{
    if (!nvs_initialized) return;

    esp_err_t ret = nvs_set_blob(nvs_handle, NVS_KEY, data, sizeof(calibration_data_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save calibration: %s", esp_err_to_name(ret));
        return;
    }
    nvs_commit(nvs_handle);
    ESP_LOGI(TAG, "Calibration saved: joy1(%d,%d) joy2(%d,%d)",
             data->joy_offsets[0].raw_x, data->joy_offsets[0].raw_y,
             data->joy_offsets[1].raw_x, data->joy_offsets[1].raw_y);
}

bool calibration_load(calibration_data_t *data)
{
    if (!nvs_initialized) return false;

    size_t required_size = sizeof(calibration_data_t);
    esp_err_t ret = nvs_get_blob(nvs_handle, NVS_KEY, data, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No calibration data found");
        data->calibrated = false;
        memset(data->joy_offsets, 0, sizeof(data->joy_offsets));
        return false;
    }

    ESP_LOGI(TAG, "Calibration loaded: joy1(%d,%d) joy2(%d,%d)",
             data->joy_offsets[0].raw_x, data->joy_offsets[0].raw_y,
             data->joy_offsets[1].raw_x, data->joy_offsets[1].raw_y);
    return data->calibrated;
}

void calibration_set_zero(const int16_t joy1_x, const int16_t joy1_y,
                          const int16_t joy2_x, const int16_t joy2_y,
                          calibration_data_t *data)
{
    data->joy_offsets[0].raw_x = joy1_x;
    data->joy_offsets[0].raw_y = joy1_y;
    data->joy_offsets[1].raw_x = joy2_x;
    data->joy_offsets[1].raw_y = joy2_y;
    data->calibrated = true;

    calibration_save(data);

    ESP_LOGI(TAG, "Zero position set: joy1(%d,%d) joy2(%d,%d)",
             joy1_x, joy1_y, joy2_x, joy2_y);
}

int16_t calibration_apply_offset(int16_t raw, int16_t offset)
{
    int32_t result = (int32_t)raw - (int32_t)offset;
    if (result > 32767) result = 32767;
    if (result < -32768) result = -32768;
    return (int16_t)result;
}

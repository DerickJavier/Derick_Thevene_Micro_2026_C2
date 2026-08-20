#pragma once

#include "input.h"
#include "mpu6050.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(MQTT_APP_EVENT);

enum {
    MQTT_APP_EVENT_CONNECTED,
    MQTT_APP_EVENT_DISCONNECTED,
};

typedef struct {
    bool wifi_connected;
    bool mqtt_connected;
} mqtt_status_t;

void mqtt_app_init(void);
void mqtt_app_start_wifi(const char *ssid, const char *password);
void mqtt_app_start_mqtt(const char *broker_ip, int broker_port);
void mqtt_app_publish_data(const input_data_t *input, const mpu6050_data_t *sensor);
mqtt_status_t mqtt_app_get_status(void);

#include "mqtt_app.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MQTT_APP"

ESP_EVENT_DEFINE_BASE(MQTT_APP_EVENT);

static esp_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_status_t app_status = {0};

#define MQTT_TOPIC_DATA "kakata/controller/data"
#define MQTT_TOPIC_STATUS "kakata/controller/status"

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        app_status.wifi_connected = false;
        esp_event_post(MQTT_APP_EVENT, MQTT_APP_EVENT_DISCONNECTED, NULL, 0, 0);
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        app_status.wifi_connected = true;
        esp_event_post(MQTT_APP_EVENT, MQTT_APP_EVENT_CONNECTED, NULL, 0, 0);
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            app_status.mqtt_connected = true;
            esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_STATUS, "online", 0, 1, 1);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            app_status.mqtt_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;
        default:
            break;
    }
}

void mqtt_app_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t wifi_inst;
    esp_event_handler_instance_t ip_inst;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &wifi_inst));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &ip_inst));

    ESP_LOGI(TAG, "WiFi/MQTT system initialized");
}

void mqtt_app_start_wifi(const char *ssid, const char *password)
{
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA mode started, SSID: %s", ssid);
}

void mqtt_app_start_mqtt(const char *broker_ip, int broker_port)
{
    char uri[64];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", broker_ip, broker_port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = "kakata_rc433",
        .session.last_will = {
            .topic = MQTT_TOPIC_STATUS,
            .msg = "offline",
            .msg_len = 7,
            .qos = 1,
            .retain = 1,
        },
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "MQTT client started, broker: %s:%d", broker_ip, broker_port);
}

void mqtt_app_publish_data(const input_data_t *input, const mpu6050_data_t *sensor)
{
    if (!app_status.mqtt_connected || !mqtt_client) return;

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"joy1\":{\"x\":%d,\"y\":%d},"
             "\"joy2\":{\"x\":%d,\"y\":%d},"
             "\"gyro\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
             "\"accel\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
             "\"buttons\":%lu,"
             "\"battery\":%.2f}",
             input->joy1_x, input->joy1_y,
             input->joy2_x, input->joy2_y,
             sensor->gyro_x, sensor->gyro_y, sensor->gyro_z,
             sensor->accel_x, sensor->accel_y, sensor->accel_z,
             (unsigned long)input->button_state,
             input->battery_voltage);

    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_DATA, payload, 0, 0, 0);
}

mqtt_status_t mqtt_app_get_status(void)
{
    return app_status;
}

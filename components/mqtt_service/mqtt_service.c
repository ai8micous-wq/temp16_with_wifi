#include "mqtt_service.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "temp_sample.h"
#include "wifi_service.h"

static const char *TAG = "mqtt_service";
static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static system_config_t s_cfg;

static void make_topic(char *buf, size_t len, const char *suffix)
{
    snprintf(buf, len, "iot/temp16/%s/%s", s_cfg.device_id, suffix);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        {
            char topic[128];
            make_topic(topic, sizeof(topic), "down/config");
            esp_mqtt_client_subscribe(s_client, topic, 1);
            make_topic(topic, sizeof(topic), "down/cmd");
            esp_mqtt_client_subscribe(s_client, topic, 1);
            make_topic(topic, sizeof(topic), "down/time");
            esp_mqtt_client_subscribe(s_client, topic, 1);
        }
        ESP_LOGI(TAG, "MQTT connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    default:
        break;
    }
}

esp_err_t mqtt_service_start(const system_config_t *cfg)
{
    if (s_client) return ESP_OK;
    s_cfg = *cfg;
    esp_mqtt_client_config_t mc = {
        .broker.address.uri = s_cfg.mqtt_uri,
        .credentials.username = s_cfg.mqtt_user,
        .credentials.authentication.password = s_cfg.mqtt_pass,
    };
    s_client = esp_mqtt_client_init(&mc);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(s_client);
}

bool mqtt_service_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_service_publish_latest(void)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    temp_frame_t frame;
    if (!temp_sample_get_latest(&frame)) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "msg_id", "tm_runtime");
    cJSON_AddNumberToObject(root, "ts", frame.ts);
    cJSON_AddStringToObject(root, "device_id", s_cfg.device_id);
    cJSON_AddNumberToObject(root, "frame_seq", frame.seq);
    cJSON_AddStringToObject(root, "data_type", "temp_batch");
    cJSON *arr = cJSON_AddArrayToObject(root, "channels");
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        cJSON *it = cJSON_CreateObject();
        cJSON_AddNumberToObject(it, "ch", i + 1);
        cJSON_AddNumberToObject(it, "temp_x10", frame.channels[i].temp_x10);
        cJSON_AddNumberToObject(it, "fault", frame.channels[i].fault);
        cJSON_AddItemToArray(arr, it);
    }
    char *json = cJSON_PrintUnformatted(root);
    char topic[128];
    make_topic(topic, sizeof(topic), "up/telemetry");
    esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t mqtt_service_publish_full_config(const system_config_t *sys, const channel_label_t labels[APP_CHANNEL_COUNT], const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "msg_id", "cfg_runtime");
    cJSON_AddNumberToObject(root, "ts", esp_log_timestamp() / 1000);
    cJSON_AddStringToObject(root, "device_id", sys->device_id);
    cJSON_AddStringToObject(root, "type", "full_config");
    cJSON_AddStringToObject(root, "device_name", sys->device_name);
    cJSON *arr = cJSON_AddArrayToObject(root, "channels");
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        cJSON *it = cJSON_CreateObject();
        cJSON_AddNumberToObject(it, "ch", i + 1);
        cJSON_AddStringToObject(it, "label", labels[i].label);
        cJSON_AddBoolToObject(it, "alarm_en", alarms[i].alarm_en);
        cJSON_AddNumberToObject(it, "high_x10", alarms[i].high_x10);
        cJSON_AddNumberToObject(it, "low_x10", alarms[i].low_x10);
        cJSON_AddItemToArray(arr, it);
    }
    char *json = cJSON_PrintUnformatted(root);
    char topic[128];
    make_topic(topic, sizeof(topic), "up/config");
    esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);
    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t mqtt_service_request_time_sync(void)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    char topic[128];
    make_topic(topic, sizeof(topic), "up/event");
    const char *json = "{\"event\":\"time_sync_request\"}";
    esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);
    return ESP_OK;
}

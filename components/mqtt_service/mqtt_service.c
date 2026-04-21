#include "mqtt_service.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "storage_service.h"
#include "temp_sample.h"
#include "wifi_service.h"
#include "app_config.h"
#include "app_events.h"

static const char *TAG = "mqtt_service";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static system_config_t s_cfg;
static system_config_t *s_sys_cfg;
static channel_label_t *s_labels;
static channel_alarm_cfg_t *s_alarms;
static uint32_t s_ack_seq;
static TaskHandle_t s_replay_task;
static uint32_t s_last_time_sync_ts;

typedef struct {
    bool active;
    int msg_id;
    uint16_t seq;
    uint32_t publish_ts;
    uint32_t broker_ack_ts;
    uint8_t retry_count;
} replay_publish_state_t;

static replay_publish_state_t s_replay_publish;

#define CACHE_ACK_TIMEOUT_S 15
#define CACHE_ACK_MAX_RETRY_LOG_INTERVAL 5

enum {
    ACK_OK = 0,
    ACK_INVALID_JSON = 1001,
    ACK_MISSING_FIELD = 1002,
    ACK_INVALID_CHANNEL = 1003,
    ACK_INVALID_PARAM = 1004,
    ACK_UNSUPPORTED = 1005,
    ACK_INTERNAL_ERROR = 1099,
};

static void make_topic(char *buf, size_t len, const char *suffix)
{
    snprintf(buf, len, "iot/temp16/%s/%s", s_cfg.device_id, suffix);
}

static uint32_t now_ts(void)
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    if (tv.tv_sec > 0) {
        return (uint32_t)tv.tv_sec;
    }
    return (uint32_t)(esp_log_timestamp() / 1000);
}

static const cJSON *json_get_required(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return item;
}

static esp_err_t post_app_event(app_event_id_t event_id)
{
    return esp_event_post(APP_EVENT, event_id, NULL, 0, portMAX_DELAY);
}

static bool replay_publish_wait_expired(void)
{
    if (!s_replay_publish.active) {
        return false;
    }

    uint32_t base_ts = s_replay_publish.broker_ack_ts != 0 ? s_replay_publish.broker_ack_ts : s_replay_publish.publish_ts;
    if (base_ts == 0) {
        return false;
    }

    uint32_t elapsed = now_ts() - base_ts;
    return elapsed >= CACHE_ACK_TIMEOUT_S;
}

static void publish_json_to_suffix(const char *suffix, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    if (!json) {
        cJSON_Delete(root);
        return;
    }

    char topic[128];
    make_topic(topic, sizeof(topic), suffix);
    esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);
    cJSON_free(json);
    cJSON_Delete(root);
}

static int publish_json_to_suffix_with_result(const char *suffix, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    if (!json) {
        cJSON_Delete(root);
        return -1;
    }

    char topic[128];
    make_topic(topic, sizeof(topic), suffix);
    int msg_id = esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);
    cJSON_free(json);
    cJSON_Delete(root);
    return msg_id;
}

static void publish_ack(const char *reply_to, int result, const char *message, int ch)
{
    if (!s_connected || !reply_to || !message) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }

    char ack_id[32];
    snprintf(ack_id, sizeof(ack_id), "ack_%06lu", (unsigned long)++s_ack_seq);
    cJSON_AddStringToObject(root, "msg_id", ack_id);
    cJSON_AddNumberToObject(root, "ts", now_ts());
    cJSON_AddStringToObject(root, "device_id", s_cfg.device_id);
    cJSON_AddStringToObject(root, "reply_to", reply_to);
    cJSON_AddNumberToObject(root, "result", result);
    cJSON_AddStringToObject(root, "message", message);
    if (ch > 0) {
        cJSON_AddNumberToObject(root, "ch", ch);
    }
    publish_json_to_suffix("up/ack", root);
}

static void publish_event(const char *event_name, const char *level, int ch)
{
    if (!s_connected || !event_name || !level) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }

    char msg_id[32];
    snprintf(msg_id, sizeof(msg_id), "evt_%06lu", (unsigned long)++s_ack_seq);
    cJSON_AddStringToObject(root, "msg_id", msg_id);
    cJSON_AddNumberToObject(root, "ts", now_ts());
    cJSON_AddStringToObject(root, "device_id", s_cfg.device_id);
    cJSON_AddStringToObject(root, "event", event_name);
    cJSON_AddStringToObject(root, "level", level);
    if (ch > 0) {
        cJSON_AddNumberToObject(root, "ch", ch);
    }
    publish_json_to_suffix("up/event", root);
}

static cJSON *build_record_payload(const temp_record_t *rec, bool from_cache)
{
    if (!rec) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    char msg_id[32];
    snprintf(msg_id, sizeof(msg_id), "%s_%u", from_cache ? "tm_cache" : "tm_live", rec->seq);
    cJSON_AddStringToObject(root, "msg_id", msg_id);
    cJSON_AddNumberToObject(root, "ts", rec->ts);
    cJSON_AddStringToObject(root, "device_id", s_cfg.device_id);
    cJSON_AddNumberToObject(root, "frame_seq", rec->seq);
    cJSON_AddStringToObject(root, "data_type", "temp_batch");
    cJSON_AddBoolToObject(root, "from_cache", from_cache);
    cJSON_AddNumberToObject(root, "cache_pending", (double)storage_service_pending_count());

    cJSON *arr = cJSON_AddArrayToObject(root, "channels");
    if (!arr) {
        cJSON_Delete(root);
        return NULL;
    }

    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        cJSON *it = cJSON_CreateObject();
        if (!it) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddNumberToObject(it, "ch", i + 1);
        cJSON_AddNumberToObject(it, "temp_x10", rec->temp_x10[i]);
        cJSON_AddNumberToObject(it, "fault", rec->fault[i]);
        cJSON_AddItemToArray(arr, it);
    }

    return root;
}

static esp_err_t publish_oldest_cached_record(void)
{
    if (!s_connected || !s_client) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_replay_publish.active) {
        return ESP_ERR_INVALID_STATE;
    }

    temp_record_t rec = {0};
    if (!storage_service_peek_oldest(&rec)) {
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *root = build_record_payload(&rec, true);
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    int msg_id = publish_json_to_suffix_with_result("up/telemetry", root);
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    s_replay_publish.active = true;
    s_replay_publish.msg_id = msg_id;
    s_replay_publish.seq = rec.seq;
    s_replay_publish.publish_ts = now_ts();
    s_replay_publish.broker_ack_ts = 0;
    s_replay_publish.retry_count++;
    ESP_LOGI(TAG, "queued cached telemetry seq=%u msg_id=%d pending=%u",
             rec.seq, msg_id, (unsigned)storage_service_pending_count());
    return ESP_OK;
}

static void replay_task(void *arg)
{
    (void)arg;

    while (1) {
        if (!s_connected || !s_client) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (s_replay_publish.active) {
            if (replay_publish_wait_expired()) {
                ESP_LOGW(TAG, "cached telemetry business ack timeout seq=%u broker_acked=%s retry=%u, will retry",
                         s_replay_publish.seq,
                         s_replay_publish.broker_ack_ts != 0 ? "yes" : "no",
                         (unsigned)s_replay_publish.retry_count);
                memset(&s_replay_publish, 0, sizeof(s_replay_publish));
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (storage_service_pending_count() == 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        esp_err_t err = publish_oldest_cached_record();
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "cached replay publish failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t mqtt_service_publish_status(const char *type)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!type || !s_sys_cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    char msg_id[32];
    char ip[32] = "0.0.0.0";
    snprintf(msg_id, sizeof(msg_id), "st_%06lu", (unsigned long)++s_ack_seq);
    if (wifi_service_get_ip_string(ip, sizeof(ip)) != ESP_OK) {
        strlcpy(ip, "0.0.0.0", sizeof(ip));
    }

    cJSON_AddStringToObject(root, "msg_id", msg_id);
    cJSON_AddNumberToObject(root, "ts", now_ts());
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "device_id", s_cfg.device_id);
    cJSON_AddStringToObject(root, "device_name", s_sys_cfg->device_name);
    cJSON_AddStringToObject(root, "fw_ver", APP_FW_VERSION);
    cJSON_AddStringToObject(root, "hw_ver", APP_HW_VERSION);
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddNumberToObject(root, "rssi", wifi_service_get_rssi());
    cJSON_AddBoolToObject(root, "ble_provisioned", s_sys_cfg->wifi_ssid[0] != '\0');
    cJSON_AddNumberToObject(root, "cache_pending", (double)storage_service_pending_count());
    cJSON_AddNumberToObject(root, "last_time_sync_ts", s_last_time_sync_ts);

    publish_json_to_suffix("up/status", root);
    return ESP_OK;
}

static bool parse_channel_1based(const cJSON *root, int *channel_out)
{
    const cJSON *ch = json_get_required(root, "ch");
    if (!cJSON_IsNumber(ch)) {
        return false;
    }
    if (ch->valueint < 1 || ch->valueint > APP_CHANNEL_COUNT) {
        return false;
    }
    *channel_out = ch->valueint;
    return true;
}

static esp_err_t apply_channel_label(const cJSON *root, const char **message_out, int *channel_out)
{
    const cJSON *label = json_get_required(root, "label");
    int ch = 0;
    if (!parse_channel_1based(root, &ch)) {
        *message_out = "invalid_channel";
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsString(label) || !label->valuestring || label->valuestring[0] == '\0') {
        *message_out = "invalid_label";
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(s_labels[ch - 1].label, label->valuestring, sizeof(s_labels[ch - 1].label));
    *channel_out = ch;
    *message_out = "channel_label_updated";
    return ESP_OK;
}

static esp_err_t apply_channel_alarm(const cJSON *root, const char **message_out, int *channel_out)
{
    int ch = 0;
    const cJSON *alarm_en = json_get_required(root, "alarm_en");
    const cJSON *high_x10 = json_get_required(root, "high_x10");
    const cJSON *low_x10 = json_get_required(root, "low_x10");

    if (!parse_channel_1based(root, &ch)) {
        *message_out = "invalid_channel";
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsBool(alarm_en) || !cJSON_IsNumber(high_x10) || !cJSON_IsNumber(low_x10)) {
        *message_out = "invalid_alarm_payload";
        return ESP_ERR_INVALID_ARG;
    }
    if (low_x10->valueint > high_x10->valueint) {
        *message_out = "invalid_alarm_range";
        return ESP_ERR_INVALID_ARG;
    }

    s_alarms[ch - 1].alarm_en = cJSON_IsTrue(alarm_en);
    s_alarms[ch - 1].high_x10 = (int16_t)high_x10->valueint;
    s_alarms[ch - 1].low_x10 = (int16_t)low_x10->valueint;
    *channel_out = ch;
    *message_out = "channel_alarm_updated";
    return ESP_OK;
}

static esp_err_t apply_device_name(const cJSON *root, const char **message_out)
{
    const cJSON *device_name = json_get_required(root, "device_name");
    if (!cJSON_IsString(device_name) || !device_name->valuestring || device_name->valuestring[0] == '\0') {
        *message_out = "invalid_device_name";
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy(s_sys_cfg->device_name, device_name->valuestring, sizeof(s_sys_cfg->device_name));
    *message_out = "device_name_updated";
    return ESP_OK;
}

static esp_err_t apply_full_config(const cJSON *root, const char **message_out)
{
    const cJSON *device_name = cJSON_GetObjectItemCaseSensitive(root, "device_name");
    const cJSON *channels = json_get_required(root, "channels");

    if (device_name && (!cJSON_IsString(device_name) || !device_name->valuestring)) {
        *message_out = "invalid_device_name";
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsArray(channels)) {
        *message_out = "invalid_channels";
        return ESP_ERR_INVALID_ARG;
    }

    if (device_name && device_name->valuestring[0] != '\0') {
        strlcpy(s_sys_cfg->device_name, device_name->valuestring, sizeof(s_sys_cfg->device_name));
    }

    cJSON *it = NULL;
    cJSON_ArrayForEach(it, channels) {
        int ch = 0;
        if (!parse_channel_1based(it, &ch)) {
            *message_out = "invalid_channel";
            return ESP_ERR_INVALID_ARG;
        }

        const cJSON *label = cJSON_GetObjectItemCaseSensitive(it, "label");
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(it, "enabled");
        const cJSON *alarm_en = cJSON_GetObjectItemCaseSensitive(it, "alarm_en");
        const cJSON *high_x10 = cJSON_GetObjectItemCaseSensitive(it, "high_x10");
        const cJSON *low_x10 = cJSON_GetObjectItemCaseSensitive(it, "low_x10");

        if (label && cJSON_IsString(label) && label->valuestring) {
            strlcpy(s_labels[ch - 1].label, label->valuestring, sizeof(s_labels[ch - 1].label));
        }
        if (enabled && cJSON_IsBool(enabled)) {
            s_alarms[ch - 1].enabled = cJSON_IsTrue(enabled) ? CH_STATUS_ENABLED : CH_STATUS_DISABLED;
        }
        if (alarm_en && cJSON_IsBool(alarm_en)) {
            s_alarms[ch - 1].alarm_en = cJSON_IsTrue(alarm_en);
        }
        if (high_x10 && cJSON_IsNumber(high_x10)) {
            s_alarms[ch - 1].high_x10 = (int16_t)high_x10->valueint;
        }
        if (low_x10 && cJSON_IsNumber(low_x10)) {
            s_alarms[ch - 1].low_x10 = (int16_t)low_x10->valueint;
        }
        if (s_alarms[ch - 1].low_x10 > s_alarms[ch - 1].high_x10) {
            *message_out = "invalid_alarm_range";
            return ESP_ERR_INVALID_ARG;
        }
    }

    *message_out = "full_config_updated";
    return ESP_OK;
}

static void handle_down_config(const cJSON *root)
{
    const cJSON *msg_id = json_get_required(root, "msg_id");
    const cJSON *config_type = json_get_required(root, "config_type");
    const char *message = "invalid_payload";
    int ack_code = ACK_OK;
    int channel = 0;
    esp_err_t err = ESP_FAIL;

    if (!cJSON_IsString(msg_id) || !msg_id->valuestring ||
        !cJSON_IsString(config_type) || !config_type->valuestring) {
        publish_ack(cJSON_IsString(msg_id) ? msg_id->valuestring : "", ACK_MISSING_FIELD, "missing_field", 0);
        return;
    }
    if (!s_sys_cfg || !s_labels || !s_alarms) {
        publish_ack(msg_id->valuestring, ACK_INTERNAL_ERROR, "config_not_bound", 0);
        return;
    }

    if (strcmp(config_type->valuestring, "channel_label") == 0) {
        err = apply_channel_label(root, &message, &channel);
    } else if (strcmp(config_type->valuestring, "channel_alarm") == 0) {
        err = apply_channel_alarm(root, &message, &channel);
    } else if (strcmp(config_type->valuestring, "device_name") == 0) {
        err = apply_device_name(root, &message);
    } else if (strcmp(config_type->valuestring, "full_config") == 0) {
        err = apply_full_config(root, &message);
    } else {
        message = "unsupported_config_type";
        ack_code = ACK_UNSUPPORTED;
        publish_ack(msg_id->valuestring, ack_code, message, 0);
        return;
    }

    if (err != ESP_OK) {
        ack_code = strcmp(message, "invalid_channel") == 0 ? ACK_INVALID_CHANNEL : ACK_INVALID_PARAM;
        publish_ack(msg_id->valuestring, ack_code, message, channel);
        return;
    }

    err = post_app_event(APP_EVENT_CONFIG_CHANGED);
    if (err != ESP_OK) {
        publish_ack(msg_id->valuestring, ACK_INTERNAL_ERROR, "config_apply_failed", channel);
        return;
    }

    publish_ack(msg_id->valuestring, ACK_OK, message, channel);
}

static void handle_down_cmd(const cJSON *root)
{
    const cJSON *msg_id = json_get_required(root, "msg_id");
    const cJSON *cmd = json_get_required(root, "cmd");

    if (!cJSON_IsString(msg_id) || !msg_id->valuestring ||
        !cJSON_IsString(cmd) || !cmd->valuestring) {
        publish_ack(cJSON_IsString(msg_id) ? msg_id->valuestring : "", ACK_MISSING_FIELD, "missing_field", 0);
        return;
    }

    if (strcmp(cmd->valuestring, "report_now") == 0) {
        esp_err_t err = post_app_event(APP_EVENT_FORCE_REPORT);
        publish_ack(msg_id->valuestring, err == ESP_OK ? ACK_OK : ACK_INTERNAL_ERROR,
                    err == ESP_OK ? "report_now_accepted" : "report_now_failed", 0);
        return;
    }

    if (strcmp(cmd->valuestring, "clear_cache") == 0) {
        storage_service_clear();
        publish_ack(msg_id->valuestring, ACK_OK, "cache_cleared", 0);
        return;
    }

    if (strcmp(cmd->valuestring, "factory_reset") == 0) {
        publish_event("factory_reset_start", "warn", 0);
        publish_ack(msg_id->valuestring, ACK_OK, "factory_reset_started", 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        post_app_event(APP_EVENT_FACTORY_RESET);
        return;
    }

    if (strcmp(cmd->valuestring, "reboot") == 0) {
        publish_ack(msg_id->valuestring, ACK_OK, "reboot_started", 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        return;
    }

    if (strcmp(cmd->valuestring, "enter_ble_provision") == 0) {
        publish_ack(msg_id->valuestring, ACK_UNSUPPORTED, "enter_ble_provision_unsupported", 0);
        return;
    }

    publish_ack(msg_id->valuestring, ACK_UNSUPPORTED, "unsupported_cmd", 0);
}

static void handle_down_time(const cJSON *root)
{
    const cJSON *utc = json_get_required(root, "utc");
    const cJSON *timezone = cJSON_GetObjectItemCaseSensitive(root, "timezone");

    if (!cJSON_IsNumber(utc)) {
        publish_event("time_sync_fail", "warn", 0);
        ESP_LOGW(TAG, "down/time missing utc");
        return;
    }

    struct timeval tv = {
        .tv_sec = (time_t)utc->valuedouble,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        publish_event("time_sync_fail", "warn", 0);
        ESP_LOGW(TAG, "settimeofday failed for utc=%ld", (long)tv.tv_sec);
        return;
    }

    s_last_time_sync_ts = (uint32_t)tv.tv_sec;
    publish_event("time_sync_ok", "info", 0);
    ESP_LOGI(TAG, "time synced utc=%ld timezone=%d",
             (long)tv.tv_sec,
             cJSON_IsNumber(timezone) ? timezone->valueint : 8);
}

static void handle_down_ack(const cJSON *root)
{
    const cJSON *reply_to = json_get_required(root, "reply_to");
    const cJSON *frame_seq = cJSON_GetObjectItemCaseSensitive(root, "frame_seq");
    const cJSON *result = json_get_required(root, "result");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");

    if (!cJSON_IsNumber(result)) {
        ESP_LOGW(TAG, "ignore down/ack without numeric result");
        return;
    }

    if (!s_replay_publish.active) {
        ESP_LOGI(TAG, "ignore down/ack because no cached telemetry is inflight");
        return;
    }

    bool matched = false;
    if (cJSON_IsNumber(frame_seq)) {
        matched = (uint16_t)frame_seq->valueint == s_replay_publish.seq;
    } else if (cJSON_IsString(reply_to) && reply_to->valuestring) {
        char expected_reply_to[32];
        snprintf(expected_reply_to, sizeof(expected_reply_to), "tm_cache_%u", s_replay_publish.seq);
        matched = strcmp(reply_to->valuestring, expected_reply_to) == 0;
    }

    if (!matched) {
        ESP_LOGI(TAG, "ignore down/ack reply_to=%s frame_seq=%d while waiting seq=%u",
                 cJSON_IsString(reply_to) ? reply_to->valuestring : "",
                 cJSON_IsNumber(frame_seq) ? frame_seq->valueint : 0,
                 s_replay_publish.seq);
        return;
    }

    if (result->valueint == ACK_OK) {
        ESP_LOGI(TAG, "cached telemetry business ack ok seq=%u retry=%u msg=%s",
                 s_replay_publish.seq,
                 (unsigned)s_replay_publish.retry_count,
                 cJSON_IsString(message) ? message->valuestring : "");
        storage_service_mark_sent();
        memset(&s_replay_publish, 0, sizeof(s_replay_publish));
        return;
    }

    ESP_LOGW(TAG, "cached telemetry business ack reject seq=%u result=%d msg=%s, will retry",
             s_replay_publish.seq,
             result->valueint,
             cJSON_IsString(message) ? message->valuestring : "");
    s_replay_publish.broker_ack_ts = 0;
    s_replay_publish.publish_ts = 0;
    s_replay_publish.active = false;
}

static void handle_mqtt_data(esp_mqtt_event_handle_t event)
{
    if (event->current_data_offset != 0 || event->data_len != event->total_data_len) {
        ESP_LOGW(TAG, "fragmented mqtt packet ignored topic_len=%d data_len=%d total=%d offset=%d",
                 event->topic_len, event->data_len, event->total_data_len, event->current_data_offset);
        return;
    }

    char topic[128];
    size_t topic_len = event->topic_len < (int)sizeof(topic) - 1 ? (size_t)event->topic_len : sizeof(topic) - 1;
    memcpy(topic, event->topic, topic_len);
    topic[topic_len] = '\0';

    char *payload = calloc(1, (size_t)event->data_len + 1);
    if (!payload) {
        return;
    }
    memcpy(payload, event->data, (size_t)event->data_len);

    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGW(TAG, "invalid json on topic=%s payload=%s", topic, payload);
        free(payload);
        return;
    }

    char expected_topic[128];
    make_topic(expected_topic, sizeof(expected_topic), "down/config");
    if (strcmp(topic, expected_topic) == 0) {
        handle_down_config(root);
        cJSON_Delete(root);
        free(payload);
        return;
    }

    make_topic(expected_topic, sizeof(expected_topic), "down/cmd");
    if (strcmp(topic, expected_topic) == 0) {
        handle_down_cmd(root);
        cJSON_Delete(root);
        free(payload);
        return;
    }

    make_topic(expected_topic, sizeof(expected_topic), "down/time");
    if (strcmp(topic, expected_topic) == 0) {
        handle_down_time(root);
        cJSON_Delete(root);
        free(payload);
        return;
    }

    make_topic(expected_topic, sizeof(expected_topic), "down/ack");
    if (strcmp(topic, expected_topic) == 0) {
        handle_down_ack(root);
        cJSON_Delete(root);
        free(payload);
        return;
    }

    ESP_LOGI(TAG, "ignore mqtt down topic=%s", topic);
    cJSON_Delete(root);
    free(payload);
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
            make_topic(topic, sizeof(topic), "down/ack");
            esp_mqtt_client_subscribe(s_client, topic, 1);
        }
        ESP_LOGI(TAG, "MQTT connected");
        mqtt_service_publish_status("online");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        if (s_replay_publish.active) {
            ESP_LOGW(TAG, "drop inflight cached telemetry seq=%u due to disconnect; will replay after reconnect",
                     s_replay_publish.seq);
            memset(&s_replay_publish, 0, sizeof(s_replay_publish));
        }
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_PUBLISHED:
        if (s_replay_publish.active && event->msg_id == s_replay_publish.msg_id) {
            s_replay_publish.broker_ack_ts = now_ts();
            if ((s_replay_publish.retry_count % CACHE_ACK_MAX_RETRY_LOG_INTERVAL) == 1 ||
                s_replay_publish.retry_count <= 2) {
                ESP_LOGI(TAG, "cached telemetry broker ack seq=%u msg_id=%d, waiting business ack",
                         s_replay_publish.seq, event->msg_id);
            } else {
                ESP_LOGW(TAG, "cached telemetry broker ack seq=%u msg_id=%d, still waiting business ack retry=%u",
                         s_replay_publish.seq, event->msg_id, (unsigned)s_replay_publish.retry_count);
            }
        }
        break;
    case MQTT_EVENT_DATA:
        handle_mqtt_data(event);
        break;
    default:
        break;
    }
}

void mqtt_service_bind_config(system_config_t *sys,
                              channel_label_t labels[APP_CHANNEL_COUNT],
                              channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    s_sys_cfg = sys;
    s_labels = labels;
    s_alarms = alarms;
}

esp_err_t mqtt_service_start(const system_config_t *cfg)
{
    if (s_client) {
        return ESP_OK;
    }
    s_cfg = *cfg;
    esp_mqtt_client_config_t mc = {
        .broker.address.uri = s_cfg.mqtt_uri,
        .credentials.username = s_cfg.mqtt_user,
        .credentials.authentication.password = s_cfg.mqtt_pass,
    };
    s_client = esp_mqtt_client_init(&mc);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (!s_replay_task) {
        xTaskCreate(replay_task, "mqtt_replay", 4096, NULL, 5, &s_replay_task);
    }
    return esp_mqtt_client_start(s_client);
}

bool mqtt_service_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_service_publish_latest(void)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    temp_record_t rec = {0};
    temp_frame_t frame;
    if (!temp_sample_get_latest(&frame)) return ESP_FAIL;
    rec.ts = frame.ts;
    rec.seq = frame.seq;
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        rec.temp_x10[i] = frame.channels[i].temp_x10;
        rec.fault[i] = frame.channels[i].fault;
    }
    cJSON *root = build_record_payload(&rec, false);
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    publish_json_to_suffix("up/telemetry", root);
    return ESP_OK;
}

esp_err_t mqtt_service_publish_full_config(const system_config_t *sys,
                                           const channel_label_t labels[APP_CHANNEL_COUNT],
                                           const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "msg_id", "cfg_runtime");
    cJSON_AddNumberToObject(root, "ts", now_ts());
    cJSON_AddStringToObject(root, "device_id", sys->device_id);
    cJSON_AddStringToObject(root, "type", "full_config");
    cJSON_AddStringToObject(root, "device_name", sys->device_name);
    cJSON_AddNumberToObject(root, "report_interval_s", sys->report_interval_s);
    cJSON_AddNumberToObject(root, "time_sync_interval_h", sys->time_sync_interval_s / 3600);

    cJSON *arr = cJSON_AddArrayToObject(root, "channels");
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        cJSON *it = cJSON_CreateObject();
        cJSON_AddNumberToObject(it, "ch", i + 1);
        cJSON_AddBoolToObject(it, "enabled", alarms[i].enabled == CH_STATUS_ENABLED);
        cJSON_AddStringToObject(it, "label", labels[i].label);
        cJSON_AddBoolToObject(it, "alarm_en", alarms[i].alarm_en);
        cJSON_AddNumberToObject(it, "high_x10", alarms[i].high_x10);
        cJSON_AddNumberToObject(it, "low_x10", alarms[i].low_x10);
        cJSON_AddItemToArray(arr, it);
    }
    publish_json_to_suffix("up/config", root);
    return ESP_OK;
}

esp_err_t mqtt_service_request_time_sync(void)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "time_sync_request");
    cJSON_AddStringToObject(root, "device_id", s_cfg.device_id);
    cJSON_AddNumberToObject(root, "ts", now_ts());
    publish_json_to_suffix("up/event", root);
    return ESP_OK;
}

uint32_t mqtt_service_get_last_time_sync_ts(void)
{
    return s_last_time_sync_ts;
}

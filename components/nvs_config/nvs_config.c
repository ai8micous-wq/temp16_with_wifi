#include "nvs_config.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "app_config.h"

#define NS_SYS    "sys"
#define NS_LABEL  "label"
#define NS_ALARM  "alarm"

static void set_defaults(system_config_t *sys, channel_label_t labels[APP_CHANNEL_COUNT], channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    memset(sys, 0, sizeof(*sys));
    strlcpy(sys->device_id, APP_DEFAULT_DEVICE_ID, sizeof(sys->device_id));
    strlcpy(sys->device_name, APP_DEFAULT_DEVICE_NAME, sizeof(sys->device_name));
    strlcpy(sys->mqtt_uri, APP_MQTT_DEFAULT_URI, sizeof(sys->mqtt_uri));
    strlcpy(sys->mqtt_user, APP_MQTT_DEFAULT_USER, sizeof(sys->mqtt_user));
    strlcpy(sys->mqtt_pass, APP_MQTT_DEFAULT_PASS, sizeof(sys->mqtt_pass));
    sys->report_interval_s = 1;
    sys->time_sync_interval_s = APP_TIME_SYNC_INTERVAL_S;
    for (int i = 0; i < APP_CHANNEL_COUNT; ++i) {
        snprintf(labels[i].label, sizeof(labels[i].label), "CH%d", i + 1);
        alarms[i].alarm_en = false;
        alarms[i].high_x10 = 0;
        alarms[i].low_x10 = 0;
    }
}

esp_err_t nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t nvs_config_load_all(system_config_t *sys, channel_label_t labels[APP_CHANNEL_COUNT], channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    set_defaults(sys, labels, alarms);
    nvs_handle_t h;
    size_t len = sizeof(*sys);
    if (nvs_open(NS_SYS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_blob(h, "cfg", sys, &len);
        nvs_close(h);
    }
    len = sizeof(channel_label_t) * APP_CHANNEL_COUNT;
    if (nvs_open(NS_LABEL, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_blob(h, "items", labels, &len);
        nvs_close(h);
    }
    len = sizeof(channel_alarm_cfg_t) * APP_CHANNEL_COUNT;
    if (nvs_open(NS_ALARM, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_blob(h, "items", alarms, &len);
        nvs_close(h);
    }
    return ESP_OK;
}

esp_err_t nvs_config_save_all(const system_config_t *sys, const channel_label_t labels[APP_CHANNEL_COUNT], const channel_alarm_cfg_t alarms[APP_CHANNEL_COUNT])
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NS_SYS, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_blob(h, "cfg", sys, sizeof(*sys)));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);

    ESP_ERROR_CHECK(nvs_open(NS_LABEL, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_blob(h, "items", labels, sizeof(channel_label_t) * APP_CHANNEL_COUNT));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);

    ESP_ERROR_CHECK(nvs_open(NS_ALARM, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_blob(h, "items", alarms, sizeof(channel_alarm_cfg_t) * APP_CHANNEL_COUNT));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_config_factory_reset(void)
{
    return nvs_flash_erase();
}

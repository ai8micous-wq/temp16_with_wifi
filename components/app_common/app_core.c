#include "app_core.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "app_config.h"
#include "app_events.h"
#include "app_types.h"
#include "board.h"
#include "lcd_proto.h"
#include "mqtt_service.h"
#include "nvs_config.h"
#include "storage_service.h"
#include "temp_sample.h"
#include "wifi_service.h"

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static const char *TAG = "app_core";
static EventGroupHandle_t s_event_group;
static system_config_t s_sys_cfg;
static channel_label_t s_labels[APP_CHANNEL_COUNT];
static channel_alarm_cfg_t s_alarms[APP_CHANNEL_COUNT];

#define WIFI_READY_BIT BIT0
#define MQTT_READY_BIT BIT1

static void publish_config_once(void)
{
    mqtt_service_publish_full_config(&s_sys_cfg, s_labels, s_alarms);
}

static void app_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != APP_EVENT) {
        return;
    }
    switch ((app_event_id_t)id) {
    case APP_EVENT_FORCE_REPORT:
        mqtt_service_publish_latest();
        break;
    case APP_EVENT_CONFIG_CHANGED:
        nvs_config_save_all(&s_sys_cfg, s_labels, s_alarms);
        lcd_proto_push_full_config(s_labels, s_alarms);
        publish_config_once();
        break;
    case APP_EVENT_TIME_SYNC_NEEDED:
        mqtt_service_request_time_sync();
        break;
    case APP_EVENT_FACTORY_RESET:
        ESP_LOGW(TAG, "Factory reset requested");
        nvs_config_factory_reset();
        esp_restart();
        break;
    default:
        break;
    }
}

static void status_task(void *arg)
{
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(s_event_group, WIFI_READY_BIT | MQTT_READY_BIT, pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(1000));
        if (bits & WIFI_READY_BIT) {
            mqtt_service_start(&s_sys_cfg);
            if (!(bits & MQTT_READY_BIT) && mqtt_service_is_connected()) {
                xEventGroupSetBits(s_event_group, MQTT_READY_BIT);
                publish_config_once();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void ui_task(void *arg)
{
    temp_frame_t frame;
    while (1) {
        if (temp_sample_get_latest(&frame)) {
            lcd_proto_push_temperatures(&frame);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_core_start(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(APP_EVENT, ESP_EVENT_ANY_ID, app_event_handler, NULL));

    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(nvs_config_init());
    ESP_ERROR_CHECK(nvs_config_load_all(&s_sys_cfg, s_labels, s_alarms));
    ESP_ERROR_CHECK(storage_service_init());
    ESP_ERROR_CHECK(lcd_proto_init());
    ESP_ERROR_CHECK(temp_sample_init(s_alarms));
    ESP_ERROR_CHECK(temp_sample_start());
    ESP_ERROR_CHECK(wifi_service_start(&s_sys_cfg));
    xEventGroupSetBits(s_event_group, WIFI_READY_BIT);

    xTaskCreate(status_task, "status_task", 4096, NULL, 4, NULL);
    xTaskCreate(ui_task, "ui_task", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Project skeleton started. FW=%s", APP_FW_VERSION);
}

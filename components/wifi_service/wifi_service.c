#include "wifi_service.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "factory_info.h"

static const char *TAG = "wifi_service";

static bool s_connected = false;
static bool s_provisioned = false;
static bool s_provisioning_active = false;
static bool s_prov_mgr_inited = false;
static esp_netif_t *s_wifi_netif = NULL;

#define PROV_QR_VERSION       "v1"
#define PROV_TRANSPORT_BLE    "ble"
#define PROV_QR_BASE_URL      "https://espressif.github.io/esp-jumpstart/qrcode.html"
#define PROV_SEC2_USERNAME    "wifiprov"
#define PROV_SEC2_POP         "abcd1234"

/*
 * Development credentials copied from Espressif's official example.
 * For production, replace this with per-device factory data so each
 * sticker carries unique username/password derived credentials.
 */
static const char s_sec2_salt[] = {
    0x03, 0x6e, 0xe0, 0xc7, 0xbc, 0xb9, 0xed, 0xa8, 0x4c, 0x9e, 0xac, 0x97, 0xd9, 0x3d, 0xec, 0xf4
};

static const char s_sec2_verifier[] = {
    0x7c, 0x7c, 0x85, 0x47, 0x65, 0x08, 0x94, 0x6d, 0xd6, 0x36, 0xaf, 0x37, 0xd7, 0xe8, 0x91, 0x43,
    0x78, 0xcf, 0xfd, 0x61, 0x6c, 0x59, 0xd2, 0xf8, 0x39, 0x08, 0x12, 0x72, 0x38, 0xde, 0x9e, 0x24,
    0xa4, 0x70, 0x26, 0x1c, 0xdf, 0xa9, 0x03, 0xc2, 0xb2, 0x70, 0xe7, 0xb1, 0x32, 0x24, 0xda, 0x11,
    0x1d, 0x97, 0x18, 0xdc, 0x60, 0x72, 0x08, 0xcc, 0x9a, 0xc9, 0x0c, 0x48, 0x27, 0xe2, 0xae, 0x89,
    0xaa, 0x16, 0x25, 0xb8, 0x04, 0xd2, 0x1a, 0x9b, 0x3a, 0x8f, 0x37, 0xf6, 0xe4, 0x3a, 0x71, 0x2e,
    0xe1, 0x27, 0x86, 0x6e, 0xad, 0xce, 0x28, 0xff, 0x54, 0x46, 0x60, 0x1f, 0xb9, 0x96, 0x87, 0xdc,
    0x57, 0x40, 0xa7, 0xd4, 0x6c, 0xc9, 0x77, 0x54, 0xdc, 0x16, 0x82, 0xf0, 0xed, 0x35, 0x6a, 0xc4,
    0x70, 0xad, 0x3d, 0x90, 0xb5, 0x81, 0x94, 0x70, 0xd7, 0xbc, 0x65, 0xb2, 0xd5, 0x18, 0xe0, 0x2e,
    0xc3, 0xa5, 0xf9, 0x68, 0xdd, 0x64, 0x7b, 0xb8, 0xb7, 0x3c, 0x9c, 0xfc, 0x00, 0xd8, 0x71, 0x7e,
    0xb7, 0x9a, 0x7c, 0xb1, 0xb7, 0xc2, 0xc3, 0x18, 0x34, 0x29, 0x32, 0x43, 0x3e, 0x00, 0x99, 0xe9,
    0x82, 0x94, 0xe3, 0xd8, 0x2a, 0xb0, 0x96, 0x29, 0xb7, 0xdf, 0x0e, 0x5f, 0x08, 0x33, 0x40, 0x76,
    0x52, 0x91, 0x32, 0x00, 0x9f, 0x97, 0x2c, 0x89, 0x6c, 0x39, 0x1e, 0xc8, 0x28, 0x05, 0x44, 0x17,
    0x3f, 0x68, 0x02, 0x8a, 0x9f, 0x44, 0x61, 0xd1, 0xf5, 0xa1, 0x7e, 0x5a, 0x70, 0xd2, 0xc7, 0x23,
    0x81, 0xcb, 0x38, 0x68, 0xe4, 0x2c, 0x20, 0xbc, 0x40, 0x57, 0x76, 0x17, 0xbd, 0x08, 0xb8, 0x96,
    0xbc, 0x26, 0xeb, 0x32, 0x46, 0x69, 0x35, 0x05, 0x8c, 0x15, 0x70, 0xd9, 0x1b, 0xe9, 0xbe, 0xcc,
    0xa9, 0x38, 0xa6, 0x67, 0xf0, 0xad, 0x50, 0x13, 0x19, 0x72, 0x64, 0xbf, 0x52, 0xc2, 0x34, 0xe2,
    0x1b, 0x11, 0x79, 0x74, 0x72, 0xbd, 0x34, 0x5b, 0xb1, 0xe2, 0xfd, 0x66, 0x73, 0xfe, 0x71, 0x64,
    0x74, 0xd0, 0x4e, 0xbc, 0x51, 0x24, 0x19, 0x40, 0x87, 0x0e, 0x92, 0x40, 0xe6, 0x21, 0xe7, 0x2d,
    0x4e, 0x37, 0x76, 0x2f, 0x2e, 0xe2, 0x68, 0xc7, 0x89, 0xe8, 0x32, 0x13, 0x42, 0x06, 0x84, 0x84,
    0x53, 0x4a, 0xb3, 0x0c, 0x1b, 0x4c, 0x8d, 0x1c, 0x51, 0x97, 0x19, 0xab, 0xae, 0x77, 0xff, 0xdb,
    0xec, 0xf0, 0x10, 0x95, 0x34, 0x33, 0x6b, 0xcb, 0x3e, 0x84, 0x0f, 0xb9, 0xd8, 0x5f, 0xb8, 0xa0,
    0xb8, 0x55, 0x53, 0x3e, 0x70, 0xf7, 0x18, 0xf5, 0xce, 0x7b, 0x4e, 0xbf, 0x27, 0xce, 0xce, 0xa8,
    0xb3, 0xbe, 0x40, 0xc5, 0xc5, 0x32, 0x29, 0x3e, 0x71, 0x64, 0x9e, 0xde, 0x8c, 0xf6, 0x75, 0xa1,
    0xe6, 0xf6, 0x53, 0xc8, 0x31, 0xa8, 0x78, 0xde, 0x50, 0x40, 0xf7, 0x62, 0xde, 0x36, 0xb2, 0xba
};

static const factory_info_record_t *get_effective_factory_record(void)
{
    static factory_info_record_t fallback;
    if (factory_info_has_valid_record()) {
        return factory_info_get();
    }

    memset(&fallback, 0, sizeof(fallback));
    strlcpy(fallback.device_id, APP_DEFAULT_DEVICE_ID, sizeof(fallback.device_id));
    strlcpy(fallback.device_name, APP_DEFAULT_DEVICE_NAME, sizeof(fallback.device_name));
    strlcpy(fallback.username, PROV_SEC2_USERNAME, sizeof(fallback.username));
    strlcpy(fallback.pop, PROV_SEC2_POP, sizeof(fallback.pop));
    fallback.salt_len = sizeof(s_sec2_salt);
    memcpy(fallback.salt, s_sec2_salt, sizeof(s_sec2_salt));
    fallback.verifier_len = sizeof(s_sec2_verifier);
    memcpy(fallback.verifier, s_sec2_verifier, sizeof(s_sec2_verifier));
    return &fallback;
}

static void wifi_start_sta(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void get_service_name(char *service_name, size_t max_len, const factory_info_record_t *factory)
{
    if (factory && factory->service_name[0] != '\0') {
        strlcpy(service_name, factory->service_name, max_len);
        return;
    }

    uint8_t mac[6] = {0};
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    snprintf(service_name, max_len, "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static void log_qr_payload(const char *service_name, const factory_info_record_t *factory)
{
    char payload[196] = {0};
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"%s\",\"name\":\"%s\",\"username\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\",\"network\":\"wifi\"}",
             PROV_QR_VERSION, service_name, factory->username, factory->pop, PROV_TRANSPORT_BLE);

    ESP_LOGI(TAG, "Provisioning started. Use Espressif provisioning app to scan the product QR sticker.");
    ESP_LOGI(TAG, "Debug QR payload: %s", payload);
    ESP_LOGI(TAG, "Debug QR URL: %s?data=%s", PROV_QR_BASE_URL, payload);
}

static esp_err_t wifi_prov_mgr_init_if_needed(void)
{
    if (s_prov_mgr_inited) {
        return ESP_OK;
    }

    network_prov_mgr_config_t prov_cfg = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };

    esp_err_t err = network_prov_mgr_init(prov_cfg);
    if (err == ESP_OK) {
        s_prov_mgr_inited = true;
    }
    return err;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == NETWORK_PROV_EVENT) {
        switch (id) {
        case NETWORK_PROV_START:
            s_provisioning_active = true;
            ESP_LOGI(TAG, "Provisioning service started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV: {
            const wifi_sta_config_t *wifi_sta_cfg = (const wifi_sta_config_t *)data;
            ESP_LOGI(TAG, "Received Wi-Fi credentials, ssid=%s", (const char *)wifi_sta_cfg->ssid);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_FAIL: {
            const network_prov_wifi_sta_fail_reason_t *reason =
                (const network_prov_wifi_sta_fail_reason_t *)data;
            ESP_LOGW(TAG, "Provisioning Wi-Fi connect failed: %s",
                     (*reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) ? "auth error" : "AP not found");
            break;
        }
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            s_provisioned = true;
            ESP_LOGI(TAG, "Provisioning successful");
            break;
        case NETWORK_PROV_END:
            s_provisioning_active = false;
            if (s_prov_mgr_inited) {
                network_prov_mgr_deinit();
                s_prov_mgr_inited = false;
            }
            ESP_LOGI(TAG, "Provisioning service stopped");
            break;
        default:
            break;
        }
        return;
    }

    if (base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (id) {
        case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
            ESP_LOGI(TAG, "Provisioning BLE connected");
            break;
        case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
            ESP_LOGI(TAG, "Provisioning BLE disconnected");
            break;
        default:
            break;
        }
        return;
    }

    if (base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (id) {
        case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
            ESP_LOGI(TAG, "Provisioning secure session established");
            break;
        case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
            ESP_LOGW(TAG, "Provisioning rejected invalid security parameters");
            break;
        case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
            ESP_LOGW(TAG, "Provisioning username/password mismatch");
            break;
        default:
            break;
        }
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        esp_wifi_connect();
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        s_connected = true;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_service_start(const system_config_t *cfg)
{
    (void)cfg;

    ESP_ERROR_CHECK(esp_netif_init());

    if (s_wifi_netif == NULL) {
        s_wifi_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    ESP_ERROR_CHECK(wifi_prov_mgr_init_if_needed());

    ESP_ERROR_CHECK(network_prov_mgr_is_wifi_provisioned(&s_provisioned));

    if (s_provisioned) {
        ESP_LOGI(TAG, "Wi-Fi already provisioned, starting station");
        network_prov_mgr_deinit();
        s_prov_mgr_inited = false;
        s_provisioning_active = false;
        wifi_start_sta();
        return ESP_OK;
    }

    const factory_info_record_t *factory = get_effective_factory_record();
    char service_name[FACTORY_INFO_SERVICE_NAME_MAX] = {0};
    get_service_name(service_name, sizeof(service_name), factory);

    network_prov_security2_params_t sec2_params = {
        .salt = (const char *)factory->salt,
        .salt_len = factory->salt_len,
        .verifier = (const char *)factory->verifier,
        .verifier_len = factory->verifier_len,
    };

    ESP_LOGW(TAG, "Wi-Fi is not provisioned, entering BLE provisioning mode");
    s_provisioning_active = true;
    if (!factory_info_has_valid_record()) {
        ESP_LOGW(TAG, "Factory provisioning data missing, falling back to development Security2 credentials");
    }
    ESP_LOGI(TAG, "Provisioning BLE service name: %s", service_name);
    ESP_LOGI(TAG, "Provisioning username: %s", factory->username);

    ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_2,
                                                        &sec2_params,
                                                        service_name,
                                                        NULL));
    log_qr_payload(service_name, factory);

    return ESP_OK;
}

esp_err_t wifi_service_reset_provisioning_and_restart(void)
{
    ESP_LOGW(TAG, "Reset provisioning requested");

    esp_err_t err = wifi_prov_mgr_init_if_needed();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "provisioning manager init failed during reset: %s", esp_err_to_name(err));
        return err;
    }

    err = network_prov_mgr_reset_wifi_provisioning();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear provisioning failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGW(TAG, "Provisioning info cleared, restarting");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return ESP_OK;
}

bool wifi_service_is_connected(void)
{
    return s_connected;
}

bool wifi_service_is_provisioned(void)
{
    return s_provisioned;
}

bool wifi_service_is_provisioning(void)
{
    return s_provisioning_active;
}

int8_t wifi_service_get_rssi(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return -127;
}

esp_err_t wifi_service_get_ip_string(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_t *netif = s_wifi_netif ? s_wifi_netif : esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_netif_ip_info_t ip_info = {0};
    esp_err_t err = esp_netif_get_ip_info(netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }

    return esp_ip4addr_ntoa(&ip_info.ip, buf, len) ? ESP_OK : ESP_FAIL;
}

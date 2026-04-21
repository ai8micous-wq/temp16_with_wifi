#include "factory_info.h"
#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define FACTORY_INFO_MAGIC                  0x46505256UL
#define FACTORY_INFO_VERSION                1U

static const char *TAG = "factory_info";
static factory_info_record_t s_record;
static bool s_record_valid = false;

static bool record_lengths_valid(const factory_info_record_t *record)
{
    return record->username[0] != '\0' &&
           record->pop[0] != '\0' &&
           record->salt_len > 0 &&
           record->salt_len <= FACTORY_INFO_SALT_MAX_LEN &&
           record->verifier_len > 0 &&
           record->verifier_len <= FACTORY_INFO_VERIFIER_MAX_LEN;
}

esp_err_t factory_info_init(void)
{
    memset(&s_record, 0, sizeof(s_record));
    s_record_valid = false;

    esp_err_t err = nvs_flash_init_partition(FACTORY_INFO_PARTITION_NAME);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(FACTORY_INFO_PARTITION_NAME));
        err = nvs_flash_init_partition(FACTORY_INFO_PARTITION_NAME);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "factory partition init failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t h = 0;
    err = nvs_open_from_partition(FACTORY_INFO_PARTITION_NAME, FACTORY_INFO_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "factory provisioning record not programmed yet");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "factory partition open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t len = sizeof(s_record);
    err = nvs_get_blob(h, FACTORY_INFO_KEY_RECORD, &s_record, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "factory provisioning blob missing");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "factory provisioning read failed: %s", esp_err_to_name(err));
        return err;
    }
    if (len != sizeof(s_record)) {
        ESP_LOGW(TAG, "factory provisioning size mismatch: %u", (unsigned)len);
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_record.magic != FACTORY_INFO_MAGIC || s_record.version != FACTORY_INFO_VERSION) {
        ESP_LOGW(TAG, "factory provisioning header invalid: magic=0x%08" PRIx32 ", ver=%u",
                 s_record.magic, s_record.version);
        memset(&s_record, 0, sizeof(s_record));
        return ESP_OK;
    }
    if (!record_lengths_valid(&s_record)) {
        ESP_LOGW(TAG, "factory provisioning credential lengths invalid");
        memset(&s_record, 0, sizeof(s_record));
        return ESP_OK;
    }

    s_record_valid = true;
    ESP_LOGI(TAG, "factory provisioning loaded: device_id=%s, service=%s, user=%s",
             s_record.device_id,
             s_record.service_name,
             s_record.username);
    return ESP_OK;
}

const factory_info_record_t *factory_info_get(void)
{
    return &s_record;
}

bool factory_info_has_valid_record(void)
{
    return s_record_valid;
}

#include "storage_service.h"
#include <string.h>
#include "esp_partition.h"
#include "esp_log.h"

static const char *TAG = "storage_service";
static const esp_partition_t *s_part;

typedef struct {
    uint32_t magic;
    uint32_t record_size;
    uint32_t max_records;
    uint32_t write_index;
    uint32_t send_index;
} cache_meta_t;

static cache_meta_t s_meta;

#define CACHE_MAGIC 0x54454D50U
#define META_OFFSET 0
#define DATA_OFFSET 4096

static uint32_t slot_offset(uint32_t index)
{
    return DATA_OFFSET + index * sizeof(temp_record_t);
}

static void persist_meta(void)
{
    esp_partition_erase_range(s_part, META_OFFSET, 4096);
    esp_partition_write(s_part, META_OFFSET, &s_meta, sizeof(s_meta));
}

esp_err_t storage_service_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "cache");
    if (!s_part) return ESP_ERR_NOT_FOUND;
    memset(&s_meta, 0, sizeof(s_meta));
    esp_partition_read(s_part, META_OFFSET, &s_meta, sizeof(s_meta));
    if (s_meta.magic != CACHE_MAGIC) {
        s_meta.magic = CACHE_MAGIC;
        s_meta.record_size = sizeof(temp_record_t);
        s_meta.max_records = (s_part->size - DATA_OFFSET) / sizeof(temp_record_t);
        s_meta.write_index = 0;
        s_meta.send_index = 0;
        persist_meta();
    }
    ESP_LOGI(TAG, "cache part size=%lu records=%lu", (unsigned long)s_part->size, (unsigned long)s_meta.max_records);
    return ESP_OK;
}

esp_err_t storage_service_append(const temp_record_t *rec)
{
    uint32_t idx = s_meta.write_index % s_meta.max_records;
    uint32_t off = slot_offset(idx);
    if ((off % 4096) == 0) {
        esp_err_t er = esp_partition_erase_range(s_part, off, 4096);
        if (er != ESP_OK) return er;
    }
    esp_err_t err = esp_partition_write(s_part, off, rec, sizeof(*rec));
    if (err == ESP_OK) {
        s_meta.write_index++;
        if ((s_meta.write_index - s_meta.send_index) > s_meta.max_records) {
            s_meta.send_index = s_meta.write_index - s_meta.max_records;
        }
        persist_meta();
    }
    return err;
}

bool storage_service_peek_oldest(temp_record_t *rec)
{
    if (s_meta.send_index >= s_meta.write_index) return false;
    uint32_t off = slot_offset(s_meta.send_index % s_meta.max_records);
    return esp_partition_read(s_part, off, rec, sizeof(*rec)) == ESP_OK;
}

esp_err_t storage_service_mark_sent(void)
{
    if (s_meta.send_index < s_meta.write_index) {
        s_meta.send_index++;
        persist_meta();
    }
    return ESP_OK;
}

void storage_service_clear(void)
{
    s_meta.write_index = 0;
    s_meta.send_index = 0;
    persist_meta();
}

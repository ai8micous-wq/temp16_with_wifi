#include "storage_service.h"
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_partition.h"
#include "esp_log.h"

static const char *TAG = "storage_service";
static const esp_partition_t *s_part;
static SemaphoreHandle_t s_lock;
static uint32_t s_meta_seq;
static int s_meta_slot = -1;

typedef struct {
    uint32_t magic;
    uint32_t record_size;
    uint32_t max_records;
    uint32_t write_index;
    uint32_t send_index;
} cache_meta_t;

typedef struct {
    uint32_t seq;
    cache_meta_t meta;
    uint32_t crc32;
} cache_meta_page_t;

static cache_meta_t s_meta;

#define CACHE_MAGIC 0x54454D50U
#define META_PAGE_SIZE 4096
#define META_SLOT_COUNT 2
#define META_OFFSET(slot) ((slot) * META_PAGE_SIZE)
#define DATA_OFFSET (META_PAGE_SIZE * META_SLOT_COUNT)

static uint16_t crc16_simple(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= buf[i];
    }
    return crc;
}

static uint32_t meta_crc32(const cache_meta_page_t *page)
{
    return crc16_simple((const uint8_t *)page, offsetof(cache_meta_page_t, crc32));
}

static void reset_meta(void)
{
    memset(&s_meta, 0, sizeof(s_meta));
    s_meta.magic = CACHE_MAGIC;
    s_meta.record_size = sizeof(temp_record_t);
    s_meta.max_records = (s_part->size - DATA_OFFSET) / sizeof(temp_record_t);
    s_meta.write_index = 0;
    s_meta.send_index = 0;
}

static bool meta_is_valid(void)
{
    if (s_meta.magic != CACHE_MAGIC) {
        return false;
    }
    if (s_meta.record_size != sizeof(temp_record_t)) {
        return false;
    }
    if (s_meta.max_records == 0) {
        return false;
    }
    if (s_meta.max_records != (s_part->size - DATA_OFFSET) / sizeof(temp_record_t)) {
        return false;
    }
    if (s_meta.send_index > s_meta.write_index) {
        return false;
    }
    if ((s_meta.write_index - s_meta.send_index) > s_meta.max_records) {
        return false;
    }
    return true;
}

static bool read_meta_page(int slot, cache_meta_page_t *page)
{
    if (!page) {
        return false;
    }
    if (esp_partition_read(s_part, META_OFFSET(slot), page, sizeof(*page)) != ESP_OK) {
        return false;
    }
    if (page->seq == 0 || page->crc32 != meta_crc32(page)) {
        return false;
    }

    cache_meta_t candidate = page->meta;
    cache_meta_t saved = s_meta;
    s_meta = candidate;
    bool ok = meta_is_valid();
    s_meta = saved;
    return ok;
}

static uint32_t slot_offset(uint32_t index)
{
    return DATA_OFFSET + index * sizeof(temp_record_t);
}

static void persist_meta(void)
{
    cache_meta_page_t page = {
        .seq = s_meta_seq + 1,
        .meta = s_meta,
        .crc32 = 0,
    };
    page.crc32 = meta_crc32(&page);

    int target_slot = (s_meta_slot + 1) % META_SLOT_COUNT;
    esp_partition_erase_range(s_part, META_OFFSET(target_slot), META_PAGE_SIZE);
    esp_partition_write(s_part, META_OFFSET(target_slot), &page, sizeof(page));
    s_meta_seq = page.seq;
    s_meta_slot = target_slot;
}

esp_err_t storage_service_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "cache");
    if (!s_part) return ESP_ERR_NOT_FOUND;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    cache_meta_page_t page = {0};
    cache_meta_page_t best_page = {0};
    bool found = false;
    for (int slot = 0; slot < META_SLOT_COUNT; ++slot) {
        if (!read_meta_page(slot, &page)) {
            continue;
        }
        if (!found || page.seq > best_page.seq) {
            best_page = page;
            s_meta_slot = slot;
            found = true;
        }
    }

    if (found) {
        s_meta = best_page.meta;
        s_meta_seq = best_page.seq;
    } else {
        ESP_LOGW(TAG, "cache meta invalid, resetting cache metadata");
        reset_meta();
        persist_meta();
    }
    ESP_LOGI(TAG, "cache part size=%lu records=%lu meta_slot=%d meta_seq=%lu",
             (unsigned long)s_part->size,
             (unsigned long)s_meta.max_records,
             s_meta_slot,
             (unsigned long)s_meta_seq);
    return ESP_OK;
}

esp_err_t storage_service_append(const temp_record_t *rec)
{
    if (!rec || !s_lock) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t idx = s_meta.write_index % s_meta.max_records;
    uint32_t off = slot_offset(idx);
    if ((off % 4096) == 0) {
        esp_err_t er = esp_partition_erase_range(s_part, off, 4096);
        if (er != ESP_OK) {
            xSemaphoreGive(s_lock);
            return er;
        }
    }
    esp_err_t err = esp_partition_write(s_part, off, rec, sizeof(*rec));
    if (err == ESP_OK) {
        s_meta.write_index++;
        if ((s_meta.write_index - s_meta.send_index) > s_meta.max_records) {
            s_meta.send_index = s_meta.write_index - s_meta.max_records;
        }
        persist_meta();
    }
    xSemaphoreGive(s_lock);
    return err;
}

bool storage_service_peek_oldest(temp_record_t *rec)
{
    if (!rec || !s_lock) {
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    while (s_meta.send_index < s_meta.write_index) {
        uint32_t current_index = s_meta.send_index;
        uint32_t off = slot_offset(current_index % s_meta.max_records);
        bool ok = esp_partition_read(s_part, off, rec, sizeof(*rec)) == ESP_OK;
        if (!ok) {
            ESP_LOGW(TAG, "read cached record failed idx=%lu, dropping", (unsigned long)current_index);
            s_meta.send_index++;
            persist_meta();
            continue;
        }

        uint16_t expected_crc = crc16_simple((const uint8_t *)rec, sizeof(*rec) - sizeof(rec->crc16));
        if (rec->crc16 != expected_crc) {
            ESP_LOGW(TAG, "cached record crc mismatch idx=%lu seq=%u expect=0x%04X got=0x%04X, dropping",
                     (unsigned long)current_index, rec->seq, expected_crc, rec->crc16);
            s_meta.send_index++;
            persist_meta();
            continue;
        }

        xSemaphoreGive(s_lock);
        return true;
    }

    xSemaphoreGive(s_lock);
    return false;
}

esp_err_t storage_service_mark_sent(void)
{
    if (!s_lock) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_meta.send_index < s_meta.write_index) {
        s_meta.send_index++;
        persist_meta();
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

size_t storage_service_pending_count(void)
{
    if (!s_lock) {
        return 0;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = (s_meta.write_index >= s_meta.send_index) ? (size_t)(s_meta.write_index - s_meta.send_index) : 0;
    xSemaphoreGive(s_lock);
    return count;
}

void storage_service_clear(void)
{
    if (!s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_meta.write_index = 0;
    s_meta.send_index = 0;
    persist_meta();
    xSemaphoreGive(s_lock);
}

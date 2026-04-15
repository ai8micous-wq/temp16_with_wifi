#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "app_types.h"

esp_err_t storage_service_init(void);
esp_err_t storage_service_append(const temp_record_t *rec);
bool storage_service_peek_oldest(temp_record_t *rec);
esp_err_t storage_service_mark_sent(void);
void storage_service_clear(void);

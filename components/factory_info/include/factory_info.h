#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_config.h"

#define FACTORY_INFO_PARTITION_NAME         "fctry"
#define FACTORY_INFO_NAMESPACE              "prov"
#define FACTORY_INFO_KEY_RECORD             "record"

#define FACTORY_INFO_SERVICE_NAME_MAX       32
#define FACTORY_INFO_USERNAME_MAX           32
#define FACTORY_INFO_POP_MAX                32
#define FACTORY_INFO_SALT_MAX_LEN           32
#define FACTORY_INFO_VERIFIER_MAX_LEN       384

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    char device_id[32];
    char device_name[APP_DEVICE_NAME_MAX];
    char service_name[FACTORY_INFO_SERVICE_NAME_MAX];
    char username[FACTORY_INFO_USERNAME_MAX];
    char pop[FACTORY_INFO_POP_MAX];
    uint16_t salt_len;
    uint16_t verifier_len;
    uint8_t salt[FACTORY_INFO_SALT_MAX_LEN];
    uint8_t verifier[FACTORY_INFO_VERIFIER_MAX_LEN];
} factory_info_record_t;

esp_err_t factory_info_init(void);
const factory_info_record_t *factory_info_get(void);
bool factory_info_has_valid_record(void);

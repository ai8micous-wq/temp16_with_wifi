idf_component_register(SRCS "wifi_service.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_event esp_netif app_common board nvs_flash bt factory_info)

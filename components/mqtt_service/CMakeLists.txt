idf_component_register(SRCS "mqtt_service.c"
                    INCLUDE_DIRS "include"
                    REQUIRES mqtt cjson esp_event app_common temp_sample wifi_service storage_service)

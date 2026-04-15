idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES app_common board cs_mux max31856 lcd_proto mqtt_service nvs_config storage_service temp_sample wifi_service esp_timer esp_event freertos)

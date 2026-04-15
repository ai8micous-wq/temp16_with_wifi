idf_component_register(SRCS "mqtt_service.c"
                    INCLUDE_DIRS "include"
                    REQUIRES mqtt cjson app_common temp_sample wifi_service)

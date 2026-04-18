idf_component_register(SRCS "lcd_proto.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_uart app_common board)

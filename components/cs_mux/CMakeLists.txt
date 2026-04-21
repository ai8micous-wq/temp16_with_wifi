idf_component_register(SRCS "cs_mux.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio
                    PRIV_REQUIRES board)

idf_component_register(SRCS "max31856.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_common esp_driver_spi spi_flash board cs_mux)

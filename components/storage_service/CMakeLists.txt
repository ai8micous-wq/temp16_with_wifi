idf_component_register(SRCS "storage_service.c"
                    INCLUDE_DIRS "include"
                    REQUIRES spi_flash app_common esp_partition)

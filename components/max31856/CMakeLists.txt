idf_component_register(SRCS "max31856.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver spi_flash board cs_mux)

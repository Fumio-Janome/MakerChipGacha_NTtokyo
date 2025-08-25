idf_component_register(SRCS "lcd_ips.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash driver spi_flash)

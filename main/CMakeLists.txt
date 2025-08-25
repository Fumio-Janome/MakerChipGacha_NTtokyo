idf_component_register(
    SRCS "lcd_ips.c" "main.c" "wifi_config.c" "web_config.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash driver spi_flash esp_wifi esp_event esp_netif esp_http_server lwip freertos
)

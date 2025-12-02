idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        freertos
        esp_driver_gpio
        esp_http_server
        esp_wifi
        esp_netif
        nvs_flash
        json
)

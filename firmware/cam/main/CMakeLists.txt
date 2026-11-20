idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp32-camera esp_http_server driver nvs_flash app_update esp_timer tn_common
)

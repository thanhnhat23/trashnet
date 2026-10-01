idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_SOURCE_DIR}/../.."
    REQUIRES esp32-camera esp_http_server esp_http_client esp_wifi nvs_flash esp_event esp_netif driver esp_timer heap
)

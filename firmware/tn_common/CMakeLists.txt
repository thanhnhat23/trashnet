idf_component_register(
    SRCS "tn_common.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_http_server esp_http_client json mdns app_update driver lwip esp_timer
)

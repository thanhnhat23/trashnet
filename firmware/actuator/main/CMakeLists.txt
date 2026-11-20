idf_component_register(
    SRCS "main.c" "servo_drv.c" "actuator.c" "http_api.c" "motion_math.c" "console.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_http_server json nvs_flash app_update esp_timer tn_common
)

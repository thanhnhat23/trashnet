#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"

#define TN_FW_VERSION "2.0.0"

typedef struct {
    const char *role;        /* "cam" | "actuator" */
    const char *role_upper;  /* "CAM" | "ACTUATOR" */
    int reset_gpio;          /* hold 5 s to erase WiFi; -1 disables */
    /* Appends role-specific JSON members (no braces, leading comma included) to /status. */
    int (*status_extra)(char *buf, size_t n);
} tn_cfg_t;

/* Bring up NVS, WiFi (STA, or SoftAP provisioning when no credentials), mDNS, UDP announce and heartbeat. */
void tn_common_start(const tn_cfg_t *cfg);
bool tn_wifi_connected(void);
/* True when no WiFi credentials exist: only the SoftAP provisioning server runs, role servers must not start. */
bool tn_is_provisioning(void);
const char *tn_device_id(void);
const char *tn_device_ip(void);

/* Control routes need X-Key when a key is stored. Sends 401 and returns false otherwise. */
bool tn_auth_ok(httpd_req_t *req);
esp_err_t tn_send_json(httpd_req_t *req, const char *json);
esp_err_t tn_send_err(httpd_req_t *req, const char *status, const char *msg);
/* Reads the request body (NUL-terminated) into buf; returns length or -1. */
int tn_read_body(httpd_req_t *req, char *buf, size_t cap);
/* Common JSON members (role,id,fw,ip,rssi,uptime,heap) without braces. */
int tn_status_common(char *buf, size_t n);
esp_err_t tn_ota_handler(httpd_req_t *req);

esp_err_t tn_nvs_get_str(const char *key, char *out, size_t cap);
esp_err_t tn_nvs_set_str(const char *key, const char *val);

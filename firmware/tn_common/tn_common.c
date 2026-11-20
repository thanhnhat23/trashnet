#include "tn_common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "tn";
#define NVS_NS "trashnet"
#define UDP_PORT 5005
#define FAIL_BEFORE_PROV 20

#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID "Long Nhat"
#define DEFAULT_WIFI_PASS "hoichirua"
#endif

static const tn_cfg_t *g_cfg;
static char g_id[13], g_ip[16] = "0.0.0.0", g_key[33], g_server[96];
static volatile bool g_connected;
static int g_retry;
static httpd_handle_t g_prov;
static bool g_prov_only;

/* ---------- NVS ---------- */
esp_err_t tn_nvs_get_str(const char *key, char *out, size_t cap) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return ESP_FAIL;
    size_t n = cap;
    esp_err_t e = nvs_get_str(h, key, out, &n);
    nvs_close(h);
    return e;
}

esp_err_t tn_nvs_set_str(const char *key, const char *val) {
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = val ? nvs_set_str(h, key, val) : nvs_erase_key(h, key);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

const char *tn_device_id(void) { return g_id; }
const char *tn_device_ip(void) { return g_ip; }
bool tn_wifi_connected(void) { return g_connected; }
bool tn_is_provisioning(void) { return g_prov_only; }

/* ---------- HTTP helpers ---------- */
bool tn_auth_ok(httpd_req_t *req) {
    if (!g_key[0]) return true;
    char v[40] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Key", v, sizeof v) == ESP_OK && strcmp(v, g_key) == 0) return true;
    tn_send_err(req, "401 Unauthorized", "unauthorized");
    return false;
}

esp_err_t tn_send_json(httpd_req_t *req, const char *json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, json);
}

esp_err_t tn_send_err(httpd_req_t *req, const char *status, const char *msg) {
    char b[96];
    snprintf(b, sizeof b, "{\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    return tn_send_json(req, b);
}

int tn_read_body(httpd_req_t *req, char *buf, size_t cap) {
    size_t total = req->content_len;
    if (total == 0 || total >= cap) return total == 0 ? 0 : -1;
    size_t got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) return -1;
        got += r;
    }
    buf[got] = 0;
    return (int)got;
}

int tn_status_common(char *buf, size_t n) {
    wifi_ap_record_t ap;
    int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    return snprintf(buf, n, "\"role\":\"%s\",\"id\":\"%s\",\"fw\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"uptime\":%lld,\"heap\":%u",
                    g_cfg->role, g_id, TN_FW_VERSION, g_ip, rssi, (long long)(esp_timer_get_time() / 1000000),
                    (unsigned)esp_get_free_heap_size());
}

esp_err_t tn_ota_handler(httpd_req_t *req) {
    if (!tn_auth_ok(req)) return ESP_OK;
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t h;
    if (!part || esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) return tn_send_err(req, "500 Internal Server Error", "ota_begin");
    static char chunk[1024];
    int left = req->content_len;
    while (left > 0) {
        int r = httpd_req_recv(req, chunk, left < (int)sizeof chunk ? left : (int)sizeof chunk);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0 || esp_ota_write(h, chunk, r) != ESP_OK) { esp_ota_abort(h); return tn_send_err(req, "500 Internal Server Error", "ota_write"); }
        left -= r;
    }
    if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) return tn_send_err(req, "500 Internal Server Error", "ota_end");
    tn_send_json(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ---------- Provisioning (SoftAP) ---------- */
static esp_err_t wifi_post(httpd_req_t *req) {
    char body[256];
    if (tn_read_body(req, body, sizeof body) <= 0) return tn_send_err(req, "400 Bad Request", "body");
    cJSON *j = cJSON_Parse(body);
    const cJSON *ssid = j ? cJSON_GetObjectItem(j, "ssid") : NULL;
    if (!cJSON_IsString(ssid) || !ssid->valuestring[0]) { cJSON_Delete(j); return tn_send_err(req, "400 Bad Request", "ssid"); }
    const cJSON *pass = cJSON_GetObjectItem(j, "pass"), *key = cJSON_GetObjectItem(j, "key"), *srv = cJSON_GetObjectItem(j, "server");
    tn_nvs_set_str("ssid", ssid->valuestring);
    tn_nvs_set_str("pass", cJSON_IsString(pass) ? pass->valuestring : "");
    tn_nvs_set_str("key", cJSON_IsString(key) && key->valuestring[0] ? key->valuestring : NULL);
    tn_nvs_set_str("server", cJSON_IsString(srv) && srv->valuestring[0] ? srv->valuestring : NULL);
    cJSON_Delete(j);
    char out[64];
    snprintf(out, sizeof out, "{\"ok\":true,\"id\":\"%s\"}", g_id);
    tn_send_json(req, out);
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

__attribute__((unused)) static esp_err_t wifi_reset_post(httpd_req_t *req) {
    if (!tn_auth_ok(req)) return ESP_OK;
    tn_nvs_set_str("ssid", NULL);
    tn_nvs_set_str("pass", NULL);
    tn_send_json(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t prov_info_get(httpd_req_t *req) {
    char b[160], c[96];
    tn_status_common(c, sizeof c);
    snprintf(b, sizeof b, "{%s,\"provisioning\":true}", c);
    return tn_send_json(req, b);
}

static esp_err_t prov_index_get(httpd_req_t *req) {
    const char *html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>TrashNet WiFi Setup</title>"
        "<style>body{font-family:system-ui,-apple-system,sans-serif;padding:24px;max-width:360px;margin:auto;background:#f8fafc;color:#0f172a}"
        "h2{margin-bottom:6px;font-size:20px}p{color:#64748b;font-size:14px;margin-top:0}"
        "input,button{width:100%;padding:12px;margin:8px 0;box-sizing:border-box;border-radius:6px;border:1px solid #cbd5e1;font-size:15px}"
        "button{background:#2563eb;color:#fff;border:none;font-weight:600;cursor:pointer;margin-top:14px}"
        "#msg{display:none;padding:12px;margin-top:14px;border-radius:6px;background:#dcfce7;color:#166534;font-size:14px}"
        "</style></head><body><h2>TrashNet WiFi Setup</h2><p>Nhập thông tin WiFi để thiết bị kết nối vào mạng.</p>"
        "<form onsubmit=\"event.preventDefault();var s=this.ssid.value,p=this.pass.value;"
        "fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s,pass:p})})"
        ".then(r=>r.json()).then(d=>{document.getElementById('msg').style.display='block';setTimeout(()=>location.reload(),4000)})"
        ".catch(e=>alert('Lỗi gửi cấu hình!'))\">"
        "<label>Tên WiFi (SSID):</label><input name=\"ssid\" required autofocus>"
        "<label>Mật khẩu WiFi:</label><input name=\"pass\" type=\"password\">"
        "<button type=\"submit\">Lưu và Kết Nối</button></form>"
        "<div id=\"msg\">Đã lưu thành công! Thiết bị đang khởi động lại...</div></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, html);
}

/* AP-only: provisioning owns port 80. AP+STA fallback: role server keeps 80, provisioning moves to 8080. */
static void start_prov_server(bool with_sta) {
    if (g_prov) return;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.server_port = with_sta ? 8080 : 80;
    c.ctrl_port = 32769;
    if (httpd_start(&g_prov, &c) != ESP_OK) return;
    httpd_uri_t post = {.uri = "/wifi", .method = HTTP_POST, .handler = wifi_post};
    httpd_uri_t info = {.uri = "/status", .method = HTTP_GET, .handler = prov_info_get};
    httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = prov_index_get};
    httpd_register_uri_handler(g_prov, &post);
    httpd_register_uri_handler(g_prov, &index);
    if (!with_sta) httpd_register_uri_handler(g_prov, &info);
}

static void start_softap(bool with_sta) {
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof ap.ap.ssid, "TrashNet-%s-%s", g_cfg->role_upper, g_id + 8);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    esp_wifi_set_mode(with_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    start_prov_server(with_sta);
    ESP_LOGW(TAG, "provisioning AP: %s", ap.ap.ssid);
}

/* ---------- STA ---------- */
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_connected = false;
        strcpy(g_ip, "0.0.0.0");
        int delay = 1000 << (g_retry < 5 ? g_retry : 5);  /* 1s .. 32s */
        if (delay > 30000) delay = 30000;
        g_retry++;
        if (g_retry == FAIL_BEFORE_PROV) start_softap(true);  /* let the user re-provision */
        vTaskDelay(pdMS_TO_TICKS(delay));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(g_ip, sizeof g_ip, IPSTR, IP2STR(&e->ip_info.ip));
        g_connected = true;
        g_retry = 0;
        ESP_LOGI(TAG, "ip %s", g_ip);
    }
}

/* ---------- Discovery + heartbeat ---------- */
static bool find_server(char *url, size_t cap) {
    if (g_server[0]) { snprintf(url, cap, "%s", g_server); return true; }
    mdns_result_t *r = NULL;
    if (mdns_query_ptr("_trashnet-server", "_tcp", 1500, 1, &r) != ESP_OK || !r) return false;
    bool ok = false;
    for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
        if (a->addr.type == ESP_IPADDR_TYPE_V4) {
            snprintf(url, cap, "http://" IPSTR ":%u", IP2STR(&a->addr.u_addr.ip4), r->port);
            ok = true;
            break;
        }
    }
    mdns_query_results_free(r);
    return ok;
}

static void announce_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(UDP_PORT), .sin_addr.s_addr = htonl(INADDR_BROADCAST)};
    for (;;) {
        if (g_connected) {
            char msg[80];
            int n = snprintf(msg, sizeof msg, "TRASHNET:%s:http://%s", g_cfg->role_upper, g_ip);
            sendto(sock, msg, n, 0, (struct sockaddr *)&to, sizeof to);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

static void heartbeat_task(void *arg) {
    static char body[320], url[128], base[100], extra[200];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        if (!g_connected || !find_server(base, sizeof base)) continue;
        int n = 0;
        char c[160];
        tn_status_common(c, sizeof c);
        extra[0] = 0;
        if (g_cfg->status_extra) g_cfg->status_extra(extra, sizeof extra);
        n = snprintf(body, sizeof body, "{%s%s}", c, extra);
        snprintf(url, sizeof url, "%s/api/v1/devices/heartbeat", base);
        esp_http_client_config_t hc = {.url = url, .method = HTTP_METHOD_POST, .timeout_ms = 3000};
        esp_http_client_handle_t cl = esp_http_client_init(&hc);
        esp_http_client_set_header(cl, "Content-Type", "application/json");
        esp_http_client_set_post_field(cl, body, n);
        esp_http_client_perform(cl);
        esp_http_client_cleanup(cl);
    }
}

static void reset_button_task(void *arg) {
    int pin = g_cfg->reset_gpio, held = 0;
    gpio_config_t io = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&io);
    for (;;) {
        held = gpio_get_level(pin) == 0 ? held + 1 : 0;
        if (held >= 50) {  /* 5 s */
            tn_nvs_set_str("ssid", NULL);
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void tn_common_start(const tn_cfg_t *cfg) {
    g_cfg = cfg;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_id, sizeof g_id, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    tn_nvs_get_str("key", g_key, sizeof g_key);
    tn_nvs_get_str("server", g_server, sizeof g_server);

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wc);
    esp_wifi_set_ps(WIFI_PS_NONE);  /* lower latency, steadier MJPEG */

    char ssid[33] = {0}, pass[65] = {0};
    if (tn_nvs_get_str("ssid", ssid, sizeof ssid) != ESP_OK || !ssid[0]) {
        if (DEFAULT_WIFI_SSID[0] != '\0') {
            strlcpy(ssid, DEFAULT_WIFI_SSID, sizeof ssid);
            strlcpy(pass, DEFAULT_WIFI_PASS, sizeof pass);
            tn_nvs_set_str("ssid", ssid);
            tn_nvs_set_str("pass", pass);
            ESP_LOGI(TAG, "initialized with default Wi-Fi: %s", ssid);
        } else {
            g_prov_only = true;
            start_softap(false);
            ESP_LOGW(TAG, "no credentials: waiting for POST /wifi");
            return;
        }
    }
    tn_nvs_get_str("pass", pass, sizeof pass);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, ssid, sizeof sta.sta.ssid);
    strlcpy((char *)sta.sta.password, pass, sizeof sta.sta.password);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_start();

    char host[32];
    snprintf(host, sizeof host, "trashnet-%s", g_cfg->role);
    mdns_init();
    mdns_hostname_set(host);
    mdns_instance_name_set(host);
    mdns_txt_item_t txt[] = {{"role", g_cfg->role}, {"id", g_id}, {"fw", TN_FW_VERSION}, {"api", "1"}};
    mdns_service_add(host, "_trashnet", "_tcp", 80, txt, 4);

    xTaskCreate(announce_task, "tn_udp", 3072, NULL, 3, NULL);
    xTaskCreate(heartbeat_task, "tn_hb", 5120, NULL, 3, NULL);
    if (g_cfg->reset_gpio >= 0) xTaskCreate(reset_button_task, "tn_btn", 2048, NULL, 2, NULL);
}

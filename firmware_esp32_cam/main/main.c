#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_camera.h"
#include "driver/gpio.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "lwip/sockets.h"

static const char *TAG = "ESP32_CAM_TRASHNET";

#include "wifi_config.h"

#define AP_SSID                 "Hoi Chieu...Troi Mua"
#define AP_PASS                 "12345678"

#define ENABLE_AUTO_UPLOAD      0
#define AUTO_UPLOAD_INTERVAL_MS 5000

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_wifi_connected = false;
static bool s_auto_upload_enabled = false;

#define FLASH_GPIO_PIN          4

#define CAM_PIN_PWDN            32
#define CAM_PIN_RESET           -1
#define CAM_PIN_XCLK            0
#define CAM_PIN_SIOD            26
#define CAM_PIN_SIOC            27
#define CAM_PIN_D7              35
#define CAM_PIN_D6              34
#define CAM_PIN_D5              39
#define CAM_PIN_D4              36
#define CAM_PIN_D3              21
#define CAM_PIN_D2              19
#define CAM_PIN_D1              18
#define CAM_PIN_D0              5
#define CAM_PIN_VSYNC           25
#define CAM_PIN_HREF            23
#define CAM_PIN_PCLK            22

static bool s_flash_on = false;

static void broadcast_device_ip(const char *dev_name, const esp_ip4_addr_t *ip)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) return;

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(5005);
    dest.sin_addr.s_addr = inet_addr("255.255.255.255");

    char msg[128];
    snprintf(msg, sizeof(msg), "TRASHNET:%s:http://" IPSTR, dev_name, IP2STR(ip));

    for (int i = 0; i < 3; i++) {
        sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    close(sock);
    ESP_LOGI(TAG, "[AUTO-IP] UDP Broadcast: %s", msg);
}

static esp_err_t init_camera(void)
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = CAM_PIN_D0;
    config.pin_d1       = CAM_PIN_D1;
    config.pin_d2       = CAM_PIN_D2;
    config.pin_d3       = CAM_PIN_D3;
    config.pin_d4       = CAM_PIN_D4;
    config.pin_d5       = CAM_PIN_D5;
    config.pin_d6       = CAM_PIN_D6;
    config.pin_d7       = CAM_PIN_D7;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 14;
    config.fb_count     = 2;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao Camera (0x%x)", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 1);
        s->set_hmirror(s, 0);
    }
    ESP_LOGI(TAG, "Khoi tao Camera OV2640 thanh cong");
    return ESP_OK;
}

#if ENABLE_AUTO_UPLOAD
static void auto_upload_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(AUTO_UPLOAD_INTERVAL_MS));
        if (!s_is_wifi_connected || !s_auto_upload_enabled) continue;

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) continue;

        esp_http_client_config_t http_config = {
            .url = "http://192.168.1.8:5001/api/predict",
            .method = HTTP_METHOD_POST,
            .timeout_ms = 4000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_config);
        if (client) {
            esp_http_client_set_header(client, "Content-Type", "image/jpeg");
            esp_http_client_set_post_field(client, (const char *)fb->buf, fb->len);
            esp_http_client_perform(client);
            esp_http_client_cleanup(client);
        }
        esp_camera_fb_return(fb);
    }
}
#endif

static esp_err_t status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"online\":true,\"device\":\"ESP32-CAM\",\"role\":\"capture_stream\",\"auto_upload\":%s}",
             s_auto_upload_enabled ? "true" : "false");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "Capture failed", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char header_buf[128];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "25");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t hlen = snprintf(header_buf, sizeof(header_buf),
                               "\r\n--" PART_BOUNDARY "\r\n"
                               "Content-Type: image/jpeg\r\n"
                               "Content-Length: %u\r\n\r\n",
                               (unsigned int)fb->len);

        res = httpd_resp_send_chunk(req, header_buf, hlen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }

        esp_camera_fb_return(fb);
        fb = NULL;

        if (res != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    return res;
}

static esp_err_t flash_handler(httpd_req_t *req)
{
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "state", val, sizeof(val)) == ESP_OK) {
            s_flash_on = (strcmp(val, "1") == 0);
            gpio_set_level(FLASH_GPIO_PIN, s_flash_on ? 1 : 0);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[48];
    snprintf(resp, sizeof(resp), "{\"flash\":%d}", s_flash_on ? 1 : 0);
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t autoupload_handler(httpd_req_t *req)
{
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "enable", val, sizeof(val)) == ESP_OK) {
            s_auto_upload_enabled = (strcmp(val, "1") == 0);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"auto_upload\":%s}", s_auto_upload_enabled ? "true" : "false");
    return httpd_resp_send(req, resp, strlen(resp));
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = status_handler };
        httpd_register_uri_handler(server, &uri_root);

        httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };
        httpd_register_uri_handler(server, &uri_status);

        httpd_uri_t uri_capture = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler };
        httpd_register_uri_handler(server, &uri_capture);

        httpd_uri_t uri_stream = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };
        httpd_register_uri_handler(server, &uri_stream);

        httpd_uri_t uri_flash = { .uri = "/flash", .method = HTTP_GET, .handler = flash_handler };
        httpd_register_uri_handler(server, &uri_flash);

        httpd_uri_t uri_autoupload = { .uri = "/autoupload", .method = HTTP_GET, .handler = autoupload_handler };
        httpd_register_uri_handler(server, &uri_autoupload);

        return server;
    }
    return NULL;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_wifi_connected = false;
        if (s_retry_num < 4) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP ESP32-CAM: http://" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        broadcast_device_ip("CAM", &event->ip_info.ip);
    }
}

static void start_softap_mode(void)
{
    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config);
    esp_wifi_start();
    ESP_LOGI(TAG, "Phat WiFi AP: %s (IP: 192.168.4.1)", AP_SSID);
}

static void init_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_max_tx_power(68);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(8000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Ket noi WiFi Station thanh cong");
    } else {
        ESP_LOGW(TAG, "Khong the noi WiFi '%s', chuyen sang AP...", WIFI_SSID);
        start_softap_mode();
    }
}

void app_main(void)
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Cấu hình GPIO Flash LED (GPIO 4)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FLASH_GPIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(FLASH_GPIO_PIN, 0);

    // 3. Khởi tạo Camera OV2640
    init_camera();

    // 4. Kết nối WiFi
    init_wifi();

    start_webserver();
#if ENABLE_AUTO_UPLOAD
    xTaskCreate(auto_upload_task, "auto_upload_task", 4096, NULL, 5, NULL);
#endif
    ESP_LOGI(TAG, "ESP32-CAM da san sang");
}

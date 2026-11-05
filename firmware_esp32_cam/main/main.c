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

static const char *TAG = "ESP32_CAM_TRASHNET";

// ====================================================================
// 1. CẤU HÌNH WIFI DÙNG CHUNG (TỪ FILE wifi_config.h Ở GỐC PROJECT)
// ====================================================================
#include "wifi_config.h"

#define AP_SSID                 "Hoi Chieu...Troi Mua"
#define AP_PASS                 "12345678"

// Địa chỉ Server Flask nhận diện AI (IP máy tính chạy app.py - Port 5001):
#define SERVER_PREDICT_URL      "http://192.168.1.8:5001/api/predict"

// Mặc định TẮT tự động đẩy ảnh để không spam terminal (Web sẽ chủ động lấy ảnh từ ESP32)
#define AUTO_UPLOAD_DEFAULT     false
#define AUTO_UPLOAD_INTERVAL_MS 5000

// ====================================================================
// GHI CHÚ VỀ SERVO:
// ESP32-CAM hiện tại KHÔNG nối Servo (không gánh tải nguồn tránh sụt áp camera).
// Mạch ESP32 thường riêng biệt sẽ kết nối WiFi và nhận lệnh từ Server để điều khiển Servo.
// ====================================================================

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_wifi_connected = false;
static bool s_auto_upload_enabled = AUTO_UPLOAD_DEFAULT;

// ====================================================================
// 2. CẤU HÌNH PHẦN CỨNG ESP32-CAM (AI-THINKER PINOUT)
// ====================================================================
#define FLASH_GPIO_PIN          4     // Đèn LED Flash siêu sáng trên ESP32-CAM

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

// ====================================================================
// 3. KHỞI TẠO CAMERA OV2640
// ====================================================================
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

    // Phân giải QVGA 320x240: Tối ưu siêu mượt, giảm độ trễ tối đa, khớp hoàn hảo kích thước 224x224 của AI
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 14;
    config.fb_count     = 2;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi khởi tạo Camera (0x%x). Vui lòng cắm chặt cáp dẹp OV2640!", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 1);
        s->set_hmirror(s, 0);
    }
    ESP_LOGI(TAG, "Khởi tạo Camera OV2640 THÀNH CÔNG!");
    return ESP_OK;
}

// ====================================================================
// 4. TASK TỰ ĐỘNG CHỤP VÀ GỬI ẢNH LÊN SERVER FLASK (/api/predict)
// ====================================================================
static void auto_upload_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Task Tự động gửi ảnh lên Server đã bắt đầu (URL: %s)", SERVER_PREDICT_URL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(AUTO_UPLOAD_INTERVAL_MS));

        if (!s_is_wifi_connected || !s_auto_upload_enabled) {
            continue;
        }

        // Lấy 1 khung hình từ camera
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGW(TAG, "[AUTO-UPLOAD] Không lấy được frame camera!");
            continue;
        }

        esp_http_client_config_t http_config = {
            .url = SERVER_PREDICT_URL,
            .method = HTTP_METHOD_POST,
            .timeout_ms = 4000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_config);
        if (client) {
            esp_http_client_set_header(client, "Content-Type", "image/jpeg");
            esp_http_client_set_post_field(client, (const char *)fb->buf, fb->len);

            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK) {
                int status_code = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "[AUTO-UPLOAD] >>> Đã gửi ảnh (%d KB) lên Server -> HTTP %d (Server đang phân loại)",
                         (int)(fb->len / 1024), status_code);
            } else {
                ESP_LOGW(TAG, "[AUTO-UPLOAD] Gửi ảnh thất bại: %s", esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);
        }

        esp_camera_fb_return(fb);
    }
}

// ====================================================================
// 5. CÁC ENDPOINT HTTP SERVER TRÊN ESP32-CAM CHO WEB APP
// ====================================================================

// 5.1 GET / và GET /status
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

// 5.2 GET /capture: Chụp 1 ảnh cho Web App khi người dùng nhấn nút Chụp
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

// 5.3 GET /stream: Live View Video trực tiếp dạng MJPEG
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    char part_buf[64];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            res = ESP_FAIL;
            break;
        }

        size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);

        esp_camera_fb_return(fb);
        fb = NULL;

        if (res != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(15)); // 15ms delay cho tốc độ khung hình mượt mà (~20-25 FPS)
    }
    return res;
}

// 5.4 GET /flash?state=1 hoặc 0: Bật/Tắt Flash LED
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

// 5.5 GET /autoupload?enable=1 hoặc 0: Bật/Tắt chế độ tự động gửi ảnh lên Server
static esp_err_t autoupload_handler(httpd_req_t *req)
{
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "enable", val, sizeof(val)) == ESP_OK) {
            s_auto_upload_enabled = (strcmp(val, "1") == 0);
            ESP_LOGI(TAG, "Chế độ Auto Upload: %s", s_auto_upload_enabled ? "BẬT" : "TẮT");
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

        ESP_LOGI(TAG, "HTTP Server đã sẵn sàng trên cổng 80!");
        return server;
    }
    return NULL;
}

// ====================================================================
// 6. QUẢN LÝ WIFI (STA + FALLBACK AP)
// ====================================================================
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
            ESP_LOGW(TAG, "Đang kết nối lại WiFi lần %d...", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "\n\n"
                      "*****************************************************************\n"
                      "**                                                             **\n"
                      "**  >>> IP ESP32-CAM: http://" IPSTR "                        **\n"
                      "**  >>> DÁN ĐỊA CHỈ NÀY VÀO GIAO DIỆN WEB TRASHNET             **\n"
                      "**                                                             **\n"
                      "*****************************************************************\n",
                      IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void start_softap_mode(void)
{
    ESP_LOGW(TAG, "Chuyển sang chế độ phát WiFi Access Point (AP)...");
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

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, ">>> ĐÃ PHÁT WIFI DỰ PHÒNG: %s (Pass: %s) <<<", AP_SSID, AP_PASS);
    ESP_LOGI(TAG, ">>> ĐỊA CHỈ TRUY CẬP: http://192.168.4.1 <<<");
    ESP_LOGI(TAG, "==================================================");
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
    esp_wifi_set_max_tx_power(68); // Giảm nhẹ công suất đỉnh tránh sụt áp nguồn USB

    ESP_LOGI(TAG, "Đang kết nối WiFi: %s ...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(8000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Kết nối WiFi Station thành công!");
    } else {
        ESP_LOGW(TAG, "Không kết nối được WiFi '%s'. Đang tự động phát AP WiFi dự phòng...", WIFI_SSID);
        start_softap_mode();
    }
}

// ====================================================================
// 7. HÀM MAIN
// ====================================================================
void app_main(void)
{
    // Vô hiệu hóa Brownout Detector (chống reset khi sụt áp nguồn USB lúc bật WiFi/Camera)
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " TRASHNET - ESP32-CAM AI CAPTURE & STREAM FIRMWARE");
    ESP_LOGI(TAG, "==================================================");

    // 1. Khởi tạo NVS Flash
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

    // 5. Khởi động Web Server HTTP trên Cổng 80
    start_webserver();

    // 6. Khởi động Task Tự động chụp & gửi ảnh lên Server phân loại AI
    xTaskCreate(auto_upload_task, "auto_upload_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ESP32-CAM đã sẵn sàng chụp ảnh và gửi về Server!");
}

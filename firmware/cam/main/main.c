/* ESP32-CAM (AI-Thinker): capture on :80, MJPEG stream on :81, flash, sensor tuning. */
#ifndef TN_STREAM_FPS
#define TN_STREAM_FPS 15
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tn_common.h"

#define FLASH_GPIO 4
static const char *TAG = "cam";
static SemaphoreHandle_t g_cam_lock;
static bool g_flash;
static char g_res[8] = "svga";
static int g_quality = 10;

static framesize_t res_of(const char *s) {
    if (!strcmp(s, "qvga")) return FRAMESIZE_QVGA;
    if (!strcmp(s, "vga")) return FRAMESIZE_VGA;
    if (!strcmp(s, "xga")) return FRAMESIZE_XGA;
    return FRAMESIZE_SVGA;
}

static void set_flash(bool on) { g_flash = on; gpio_set_level(FLASH_GPIO, on); }

static void apply_sensor(const char *res, int q) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;
    s->set_framesize(s, res_of(res));
    s->set_quality(s, q < 4 ? 4 : (q > 63 ? 63 : q));
}

static void load_tuning(void) {
    char v[16];
    if (tn_nvs_get_str("cam_res", v, sizeof v) == ESP_OK) strlcpy(g_res, v, sizeof g_res);
    if (tn_nvs_get_str("cam_q", v, sizeof v) == ESP_OK) g_quality = atoi(v);
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);
        s->set_whitebal(s, 1); s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1); s->set_aec2(s, 1);
        s->set_gain_ctrl(s, 1); s->set_lenc(s, 1);
        if (tn_nvs_get_str("cam_sharp", v, sizeof v) == ESP_OK) s->set_sharpness(s, atoi(v));
        if (tn_nvs_get_str("cam_dn", v, sizeof v) == ESP_OK) s->set_denoise(s, atoi(v));
    }
    apply_sensor(g_res, g_quality);
}

static esp_err_t cam_init(void) {
    camera_config_t c = {
        .pin_pwdn = 32, .pin_reset = -1, .pin_xclk = 0, .pin_sccb_sda = 26, .pin_sccb_scl = 27,
        .pin_d7 = 35, .pin_d6 = 34, .pin_d5 = 39, .pin_d4 = 36, .pin_d3 = 21, .pin_d2 = 19, .pin_d1 = 18, .pin_d0 = 5,
        .pin_vsync = 25, .pin_href = 23, .pin_pclk = 22,
        .xclk_freq_hz = 20000000, .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG, .frame_size = FRAMESIZE_SVGA, .jpeg_quality = 10,
        .fb_count = 2, .fb_location = CAMERA_FB_IN_PSRAM, .grab_mode = CAMERA_GRAB_LATEST,
    };
    return esp_camera_init(&c);
}

static void drop_frames(int n) {
    for (int i = 0; i < n; i++) { camera_fb_t *f = esp_camera_fb_get(); if (f) esp_camera_fb_return(f); }
}

static int qint(httpd_req_t *r, const char *k, int def) {
    char q[96], v[16];
    if (httpd_req_get_url_query_str(r, q, sizeof q) == ESP_OK && httpd_query_key_value(q, k, v, sizeof v) == ESP_OK) return atoi(v);
    return def;
}

static bool qstr(httpd_req_t *r, const char *k, char *out, size_t n) {
    char q[96];
    return httpd_req_get_url_query_str(r, q, sizeof q) == ESP_OK && httpd_query_key_value(q, k, out, n) == ESP_OK;
}

static int extra(char *b, size_t n) { return snprintf(b, n, ",\"state\":\"%s\"", g_flash ? "flash" : "idle"); }

static esp_err_t status_get(httpd_req_t *r) {
    char c[200], b[360];
    tn_status_common(c, sizeof c);
    snprintf(b, sizeof b, "{%s,\"stream_port\":81,\"flash\":%s,\"res\":\"%s\",\"quality\":%d}", c, g_flash ? "true" : "false", g_res, g_quality);
    return tn_send_json(r, b);
}

static esp_err_t capture_get(httpd_req_t *r) {
    char res[8];
    bool tmp_res = qstr(r, "res", res, sizeof res);
    int q = qint(r, "q", 0), flash = qint(r, "flash", 0), warm = qint(r, "warmup", 1);
    xSemaphoreTake(g_cam_lock, portMAX_DELAY);
    if (tmp_res || q) apply_sensor(tmp_res ? res : g_res, q ? q : g_quality);
    bool was = g_flash;
    if (flash) set_flash(true);
    drop_frames(warm < 0 ? 0 : (warm > 6 ? 6 : warm));
    camera_fb_t *fb = esp_camera_fb_get();
    if (flash && !was) set_flash(false);
    esp_err_t e = ESP_FAIL;
    if (fb) {
        httpd_resp_set_type(r, "image/jpeg");
        httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(r, "Cache-Control", "no-store");
        e = httpd_resp_send(r, (const char *)fb->buf, fb->len);
        esp_camera_fb_return(fb);
    } else {
        tn_send_err(r, "503 Service Unavailable", "no_frame");
    }
    if (tmp_res || q) apply_sensor(g_res, g_quality);
    xSemaphoreGive(g_cam_lock);
    return e;
}

static esp_err_t flash_get(httpd_req_t *r) {
    if (!tn_auth_ok(r)) return ESP_OK;
    set_flash(qint(r, "on", 0) != 0);
    char b[32]; snprintf(b, sizeof b, "{\"flash\":%s}", g_flash ? "true" : "false");
    return tn_send_json(r, b);
}

static esp_err_t cam_post(httpd_req_t *r) {
    if (!tn_auth_ok(r)) return ESP_OK;
    char v[8]; sensor_t *s = esp_camera_sensor_get();
    xSemaphoreTake(g_cam_lock, portMAX_DELAY);
    if (qstr(r, "res", v, sizeof v)) { strlcpy(g_res, v, sizeof g_res); tn_nvs_set_str("cam_res", v); }
    int q = qint(r, "q", 0);
    if (q) { g_quality = q; snprintf(v, sizeof v, "%d", q); tn_nvs_set_str("cam_q", v); }
    if (s && qstr(r, "sharp", v, sizeof v)) { s->set_sharpness(s, atoi(v)); tn_nvs_set_str("cam_sharp", v); }
    if (s && qstr(r, "denoise", v, sizeof v)) { s->set_denoise(s, atoi(v)); tn_nvs_set_str("cam_dn", v); }
    apply_sensor(g_res, g_quality);
    xSemaphoreGive(g_cam_lock);
    return status_get(r);
}

static esp_err_t stream_get(httpd_req_t *r) {
    httpd_resp_set_type(r, "multipart/x-mixed-replace;boundary=tnframe");
    httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
    char hdr[80];
    const int64_t period_us = 1000000 / TN_STREAM_FPS;
    for (;;) {
        int64_t t0 = esp_timer_get_time();
        if (!xSemaphoreTake(g_cam_lock, pdMS_TO_TICKS(500))) continue;
        camera_fb_t *fb = esp_camera_fb_get();
        xSemaphoreGive(g_cam_lock);
        if (!fb) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        int n = snprintf(hdr, sizeof hdr, "\r\n--tnframe\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)fb->len);
        esp_err_t e = httpd_resp_send_chunk(r, hdr, n);
        if (e == ESP_OK) e = httpd_resp_send_chunk(r, (const char *)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (e != ESP_OK) break;
        /* pace to TN_STREAM_FPS instead of a fixed 60 ms sleep: a slow WiFi send already used the budget */
        int64_t left_ms = (period_us - (esp_timer_get_time() - t0)) / 1000;
        vTaskDelay(pdMS_TO_TICKS(left_ms > 1 ? left_ms : 1));
    }
    return ESP_OK;
}

static httpd_handle_t start_server(int port, int ctrl, bool stream) {
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.server_port = port; c.ctrl_port = ctrl; c.max_uri_handlers = 12; c.stack_size = 6144; c.lru_purge_enable = true;
    httpd_handle_t h = NULL;
    if (httpd_start(&h, &c) != ESP_OK) return NULL;
    if (stream) {
        httpd_uri_t s = {.uri = "/stream", .method = HTTP_GET, .handler = stream_get};
        httpd_register_uri_handler(h, &s);
        return h;
    }
    httpd_uri_t u[] = {
        {.uri = "/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/capture", .method = HTTP_GET, .handler = capture_get},
        {.uri = "/flash", .method = HTTP_GET, .handler = flash_get},
        {.uri = "/cam", .method = HTTP_POST, .handler = cam_post},
        {.uri = "/ota", .method = HTTP_POST, .handler = tn_ota_handler},
    };
    for (int i = 0; i < 5; i++) httpd_register_uri_handler(h, &u[i]);
    return h;
}

void app_main(void) {
    static const tn_cfg_t cfg = {.role = "cam", .role_upper = "CAM", .reset_gpio = -1, .status_extra = extra};
    gpio_config_t io = {.pin_bit_mask = 1ULL << FLASH_GPIO, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);
    set_flash(false);
    g_cam_lock = xSemaphoreCreateMutex();
    tn_common_start(&cfg);
    if (tn_is_provisioning()) return;
    if (cam_init() != ESP_OK) { ESP_LOGE(TAG, "camera init failed"); vTaskDelay(pdMS_TO_TICKS(2000)); esp_restart(); }
    load_tuning();
    start_server(80, 32768, false);
    start_server(81, 32770, true);
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "ready");
}

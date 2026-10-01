#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_rom_sys.h"

// ====================================================================
// 1. CẤU HÌNH WIFI DÙNG CHUNG (TỪ FILE wifi_config.h Ở GỐC PROJECT)
// ====================================================================
#include "wifi_config.h"

#define WIFI_CONNECTED_BIT        BIT0
#define WIFI_FAIL_BIT             BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_wifi_connected = false;

// ====================================================================
// 2. CẤU HÌNH GPIO PHẦN CỨNG (ĐÚNG THEO SƠ ĐỒ ĐÃ NỐI DÂY)
// ====================================================================
#define SERVO_MG995_GPIO    17

#define STEPPER_IN1_GPIO    18
#define STEPPER_IN2_GPIO    19
#define STEPPER_IN3_GPIO    25
#define STEPPER_IN4_GPIO    26

#define I2C_MASTER_SDA_IO   21
#define I2C_MASTER_SCL_IO   22
#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_FREQ_HZ  50000
#define LCD_I2C_ADDR        0x3F      

#define LCD_BACKLIGHT       0x08
#define ENABLE              0x04
#define RS                  0x01

#define STEPS_PER_REV       4096
#define STEPS_PER_SECTION   (STEPS_PER_REV / 4) // 1024 bước (90 độ mỗi ngăn)
#define STEP_DELAY_US       2000 

static const char *TAG = "TRASHNET_ACTUATOR";
static int current_position = 0; // 0: Ngăn 1, 1: Ngăn 2, 2: Ngăn 3, 3: Ngăn 4
static int s_current_servo_angle = 0;
static bool s_is_busy = false;
static char s_last_trash_name[32] = "San sang";

static const uint8_t step_sequence[8][4] = {
    {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
    {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}
};

// Cấu trúc hàng đợi lệnh phân loại rác
typedef struct {
    int bin_number;             // 1 đến 4
    char trash_label[32];       // Tên kỹ thuật (ví dụ: plastic, biological)
    char trash_display_name[32];// Tên tiếng Việt hiển thị (ví dụ: Chai nhua, Thuc an)
} bin_command_t;

static QueueHandle_t s_bin_cmd_queue = NULL;

// ====================================================================
// 3. DRIVER LCD 1602 I2C
// ====================================================================
static void lcd_i2c_write_byte(uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
}

static void lcd_strobe(uint8_t data) {
    lcd_i2c_write_byte(data | ENABLE | LCD_BACKLIGHT);
    esp_rom_delay_us(500);
    lcd_i2c_write_byte((data & ~ENABLE) | LCD_BACKLIGHT);
    esp_rom_delay_us(500);
}

static void lcd_write_4bits(uint8_t value, uint8_t mode) {
    uint8_t pin_data = (value & 0xF0) | mode | LCD_BACKLIGHT;
    lcd_i2c_write_byte(pin_data);
    lcd_strobe(pin_data);
}

static void lcd_send_cmd(uint8_t cmd) {
    lcd_write_4bits(cmd & 0xF0, 0);
    lcd_write_4bits((cmd << 4) & 0xF0, 0);
    if (cmd == 0x01 || cmd == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void lcd_send_data(uint8_t data) {
    lcd_write_4bits(data & 0xF0, RS);
    lcd_write_4bits((data << 4) & 0xF0, RS);
}

static void lcd_init(void) {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    vTaskDelay(pdMS_TO_TICKS(100));

    lcd_write_4bits(0x30, 0); vTaskDelay(pdMS_TO_TICKS(10));
    lcd_write_4bits(0x30, 0); vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_4bits(0x30, 0); esp_rom_delay_us(1000);
    lcd_write_4bits(0x20, 0); esp_rom_delay_us(1000); 

    lcd_send_cmd(0x28); 
    lcd_send_cmd(0x0C); 
    lcd_send_cmd(0x06); 
    lcd_send_cmd(0x01); 
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void lcd_clear(void) {
    lcd_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void lcd_set_cursor(uint8_t row, uint8_t col) {
    uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_send_cmd(addr);
}

static void lcd_print(const char *str) {
    while (*str) {
        lcd_send_data((uint8_t)(*str));
        str++;
    }
}

static void lcd_display_lines(const char *line1, const char *line2) {
    lcd_clear();
    char buf1[17];
    char buf2[17];
    snprintf(buf1, sizeof(buf1), "%-16.16s", line1 ? line1 : "");
    snprintf(buf2, sizeof(buf2), "%-16.16s", line2 ? line2 : "");
    lcd_set_cursor(0, 0);
    lcd_print(buf1);
    lcd_set_cursor(1, 0);
    lcd_print(buf2);
}

static void lcd_show_ready(void) {
    lcd_display_lines("THUNG RAC 4 NGAN", "SAN SANG CHO RAC");
}

// ====================================================================
// 4. DRIVER ĐỘNG CƠ BƯỚC & SERVO MG995
// ====================================================================
static void hardware_init(void)
{
    // Động cơ bước 28BYJ-48
    gpio_config_t stepper_conf = {};
    stepper_conf.mode = GPIO_MODE_OUTPUT;
    stepper_conf.pin_bit_mask = (1ULL << STEPPER_IN1_GPIO) | (1ULL << STEPPER_IN2_GPIO) |
                                (1ULL << STEPPER_IN3_GPIO) | (1ULL << STEPPER_IN4_GPIO);
    gpio_config(&stepper_conf);

    // Servo MG995 điều khiển bằng PWM (LEDC)
    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode       = LEDC_LOW_SPEED_MODE;
    timer_config.duty_resolution  = LEDC_TIMER_16_BIT;
    timer_config.timer_num        = LEDC_TIMER_0;
    timer_config.freq_hz          = 50;
    timer_config.clk_cfg          = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t mg995_channel = {};
    mg995_channel.gpio_num   = SERVO_MG995_GPIO;
    mg995_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    mg995_channel.channel    = LEDC_CHANNEL_0;
    mg995_channel.timer_sel  = LEDC_TIMER_0;
    ESP_ERROR_CHECK(ledc_channel_config(&mg995_channel));

    lcd_init();
    ESP_LOGI(TAG, "Hardware Initialized!");
}

static void stepper_stop(void) {
    gpio_set_level((gpio_num_t)STEPPER_IN1_GPIO, 0);
    gpio_set_level((gpio_num_t)STEPPER_IN2_GPIO, 0);
    gpio_set_level((gpio_num_t)STEPPER_IN3_GPIO, 0);
    gpio_set_level((gpio_num_t)STEPPER_IN4_GPIO, 0);
}

static void stepper_step(int steps, int dir) {
    static int current_step_idx = 0;

    for (int i = 0; i < steps; i++) {
        current_step_idx = (dir == 1) ? ((current_step_idx + 1) % 8) : ((current_step_idx - 1 + 8) % 8);

        gpio_set_level((gpio_num_t)STEPPER_IN1_GPIO, step_sequence[current_step_idx][0]);
        gpio_set_level((gpio_num_t)STEPPER_IN2_GPIO, step_sequence[current_step_idx][1]);
        gpio_set_level((gpio_num_t)STEPPER_IN3_GPIO, step_sequence[current_step_idx][2]);
        gpio_set_level((gpio_num_t)STEPPER_IN4_GPIO, step_sequence[current_step_idx][3]);

        esp_rom_delay_us(STEP_DELAY_US);

        // Cứ sau 64 bước thì nhường CPU 1 tick để reset Watchdog
        if (i % 64 == 0) {
            vTaskDelay(1); 
        }
    }

    stepper_stop();
}

static void rotate_to_bin(int target_bin_idx) {
    if (target_bin_idx == current_position) return;

    int clockwise = (target_bin_idx - current_position + 4) % 4;
    int counter_clockwise = (current_position - target_bin_idx + 4) % 4;

    int dir = (clockwise <= counter_clockwise) ? 1 : -1;
    int sections = (clockwise <= counter_clockwise) ? clockwise : counter_clockwise;

    ESP_LOGI(TAG, "Xoay mam tu ngan %d den ngan %d (%d goc 90 do, chieu %s)",
             current_position + 1, target_bin_idx + 1, sections, dir == 1 ? "thuan" : "nghich");

    stepper_step(sections * STEPS_PER_SECTION, dir);
    current_position = target_bin_idx;
}

static void mg995_set_angle(int angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    s_current_servo_angle = angle;
    uint32_t pulse_us = 500 + ((2000) * angle) / 180;
    uint32_t duty = (pulse_us * 65535) / 20000;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// Xử lý chu trình phân loại hoàn chỉnh cho 1 ngăn rác
static void process_bin_action(int bin_number, const char *display_name) {
    const char *trash_categories[4] = {
        "HUU CO",      // Ngăn 1: Rác Hữu Cơ (biological)
        "TAI CHE",     // Ngăn 2: Rác Tái Chế (cardboard, glass, metal, paper, plastic)
        "VO CO",       // Ngăn 3: Rác Vô Cơ (trash)
        "NGUY HAI"     // Ngăn 4: Rác Nguy Hại (battery)
    };

    if (bin_number < 1 || bin_number > 4) return;
    s_is_busy = true;

    int target_idx = bin_number - 1;
    const char *category = trash_categories[target_idx];

    // Dòng 1: "NGAN 1: HUU CO  " (đúng 16 ký tự)
    char line1[17];
    snprintf(line1, sizeof(line1), "NGAN %d: %-8s", bin_number, category);

    // Dòng 2: Tên chi tiết rác (đúng 16 ký tự)
    char line2[17];
    if (display_name && strlen(display_name) > 0) {
        snprintf(line2, sizeof(line2), "%-16.16s", display_name);
    } else {
        snprintf(line2, sizeof(line2), "Rac %-12.12s", category);
    }

    strncpy(s_last_trash_name, line2, sizeof(s_last_trash_name) - 1);
    s_last_trash_name[sizeof(s_last_trash_name) - 1] = '\0';

    ESP_LOGI(TAG, ">>> BAT DAU XU LY: Ngan %d [%s] - %s", bin_number, category, line2);

    // 1. Hiển thị thông tin nhận diện lên màn hình LCD 1602
    lcd_display_lines(line1, line2);
    vTaskDelay(pdMS_TO_TICKS(1200));

    // 2. Xoay thùng rác 4 ngăn đến đúng hướng
    lcd_display_lines(line1, "DANG XOAY THUNG.");
    rotate_to_bin(target_idx);
    vTaskDelay(pdMS_TO_TICKS(300));

    // 3. Mở nắp bằng Servo MG995 là 180 độ
    ESP_LOGI(TAG, ">>> Mo nap thung rac (Servo 180 do)");
    lcd_display_lines(line1, "XIN BO RAC VAO! ");
    mg995_set_angle(180);
    vTaskDelay(pdMS_TO_TICKS(2500)); // Chờ 2.5 giây để rác rơi xuống ngăn

    // 4. Đóng nắp thùng rác lại (0 độ)
    ESP_LOGI(TAG, ">>> Dong nap thung rac (Servo 0 do)");
    lcd_display_lines(line1, "DANG DONG NAP...");
    mg995_set_angle(0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 5. Cập nhật lại LCD về trạng thái sẵn sàng
    lcd_show_ready();

    s_is_busy = false;
    ESP_LOGI(TAG, ">>> HOAN TAT CHU TRINH NGAN %d!", bin_number);
}

// Task chạy ngầm xử lý hàng đợi lệnh từ Web Server
static void actuator_worker_task(void *pvParameters) {
    bin_command_t cmd;
    while (1) {
        if (xQueueReceive(s_bin_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            process_bin_action(cmd.bin_number, cmd.trash_display_name);
        }
    }
}

// ====================================================================
// 5. HTTP SERVER ĐIỀU KHIỂN QUA MẠNG TỪ WEB APP / FLASK
// ====================================================================

// Giải mã URL (%20 -> khoảng trắng, loại bỏ ký tự ngoài ASCII)
static void url_decode(char *dst, const char *src, size_t dst_size) {
    if (!dst || !src || dst_size == 0) return;
    size_t d = 0;
    while (*src && d < dst_size - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            char *endptr = NULL;
            long val = strtol(hex, &endptr, 16);
            if (endptr == hex + 2 && val >= 32 && val <= 126) {
                dst[d++] = (char)val;
                src += 3;
                continue;
            } else if (endptr == hex + 2) {
                src += 3;
                continue;
            }
        } else if (*src == '+') {
            dst[d++] = ' ';
            src++;
            continue;
        }
        if ((unsigned char)(*src) >= 32 && (unsigned char)(*src) <= 126) {
            dst[d++] = *src;
        }
        src++;
    }
    dst[d] = '\0';
}

// GET /action?bin=1&label=plastic&name=Chai%20nhua
static esp_err_t http_action_handler(httpd_req_t *req) {
    char query[128] = {0};
    int bin = 1;
    char label[32] = "unknown";
    char name[32] = "";

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[64];
        if (httpd_query_key_value(query, "bin", val, sizeof(val)) == ESP_OK) {
            bin = atoi(val);
        }
        if (httpd_query_key_value(query, "label", val, sizeof(val)) == ESP_OK) {
            url_decode(label, val, sizeof(label));
        }
        if (httpd_query_key_value(query, "name", val, sizeof(val)) == ESP_OK) {
            url_decode(name, val, sizeof(name));
        }
    }

    if (bin < 1) bin = 1;
    if (bin > 4) bin = 4;

    bin_command_t cmd;
    cmd.bin_number = bin;
    strncpy(cmd.trash_label, label, sizeof(cmd.trash_label) - 1);
    cmd.trash_label[sizeof(cmd.trash_label) - 1] = '\0';
    strncpy(cmd.trash_display_name, (strlen(name) > 0) ? name : label, sizeof(cmd.trash_display_name) - 1);
    cmd.trash_display_name[sizeof(cmd.trash_display_name) - 1] = '\0';

    // Đẩy vào hàng đợi để task ngầm xử lý ngay lập tức
    xQueueSend(s_bin_cmd_queue, &cmd, 0);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    char resp[160];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"success\",\"bin\":%d,\"label\":\"%s\",\"message\":\"Executing bin action\"}",
             bin, label);
    return httpd_resp_send(req, resp, strlen(resp));
}

// GET /status: Trả về trạng thái hiện tại của thùng rác cho Web UI
static esp_err_t http_status_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"online\":true,\"current_bin\":%d,\"current_angle\":%d,\"servo_angle\":%d,\"is_busy\":%s,\"last_trash\":\"%s\"}",
             current_position + 1, current_position * 90, s_current_servo_angle,
             s_is_busy ? "true" : "false", s_last_trash_name);
    return httpd_resp_send(req, resp, strlen(resp));
}

// GET /servo?angle=180 (hoặc 0): Test nắp mở/đóng thủ công
static esp_err_t http_servo_handler(httpd_req_t *req) {
    char query[32] = {0};
    int angle = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "angle", val, sizeof(val)) == ESP_OK) {
            angle = atoi(val);
        }
    }
    mg995_set_angle(angle);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"servo_angle\":%d}", angle);
    return httpd_resp_send(req, resp, strlen(resp));
}

// GET /rotate?bin=1..4: Test xoay động cơ bước thủ công
static esp_err_t http_rotate_handler(httpd_req_t *req) {
    char query[32] = {0};
    int bin = 1;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "bin", val, sizeof(val)) == ESP_OK) {
            bin = atoi(val);
        }
    }
    if (bin >= 1 && bin <= 4) {
        rotate_to_bin(bin - 1);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"current_bin\":%d}", current_position + 1);
    return httpd_resp_send(req, resp, strlen(resp));
}

static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 4;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t action_uri = {
            .uri = "/action",
            .method = HTTP_GET,
            .handler = http_action_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &action_uri);

        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = http_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t servo_uri = {
            .uri = "/servo",
            .method = HTTP_GET,
            .handler = http_servo_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &servo_uri);

        httpd_uri_t rotate_uri = {
            .uri = "/rotate",
            .method = HTTP_GET,
            .handler = http_rotate_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &rotate_uri);

        ESP_LOGI(TAG, "HTTP Server da khoi dong tren port 80!");
    }
    return server;
}

// ====================================================================
// 6. XỬ LÝ SỰ KIỆN WIFI
// ====================================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_wifi_connected = false;
        if (s_retry_num < 10) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Dang thu ket noi lai WiFi lan %d...", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "\n\n"
                      "*****************************************************************\n"
                      "**                                                             **\n"
                      "**  >>> IP ESP32 ACTUATOR (SERVO + STEP + LCD):                **\n"
                      "**  >>> http://" IPSTR "                                       **\n"
                      "**  >>> DAN DIA CHI NAY VAO WEB TRASHNET DE DIEU KHIEN         **\n"
                      "**                                                             **\n"
                      "*****************************************************************\n",
                      IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Hiển thị IP lên LCD lúc khởi động thành công
        char ip_str[17];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        lcd_display_lines("WIFI DA KET NOI", ip_str);
        vTaskDelay(pdMS_TO_TICKS(1800));

        lcd_show_ready();
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA khoi tao hoan tat. Dang ket noi toi SSID: %s", CONFIG_ESP_WIFI_SSID);
}

// ====================================================================
// 7. HÀM MAIN CHÍNH
// ====================================================================
void app_main(void)
{
    // 1. Khởi tạo Flash NVS (cho WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Khởi tạo Phần cứng (Servo, Động cơ bước, LCD)
    hardware_init();
    stepper_stop();
    mg995_set_angle(0); // Đóng nắp ban đầu

    // Màn hình khởi động
    char wifi_buf[17];
    snprintf(wifi_buf, sizeof(wifi_buf), "%-16.16s", CONFIG_ESP_WIFI_SSID);
    lcd_display_lines("DANG KET NOI...", wifi_buf);

    // 3. Khởi tạo hàng đợi lệnh và Task xử lý ngầm
    s_bin_cmd_queue = xQueueCreate(10, sizeof(bin_command_t));
    xTaskCreate(actuator_worker_task, "actuator_worker", 4096, NULL, 5, NULL);

    // 4. Kết nối WiFi
    wifi_init_sta();

    // 5. Khởi động Web Server HTTP
    start_webserver();

    ESP_LOGI(TAG, "=== ESP32 ACTUATOR SYSTEM READY ===");
    ESP_LOGI(TAG, "Nhan phim 1-4 tren ban phim de test nhanh:");
    ESP_LOGI(TAG, "  1: Rac HUU CO (Ngan 1)");
    ESP_LOGI(TAG, "  2: Rac TAI CHE (Ngan 2)");
    ESP_LOGI(TAG, "  3: Rac VO CO (Ngan 3)");
    ESP_LOGI(TAG, "  4: Rac NGUY HIEM (Ngan 4)");
    ESP_LOGI(TAG, "  o: Mo nap (Servo 180)");
    ESP_LOGI(TAG, "  c: Dong nap (Servo 0)");

    // Vòng lặp nhận lệnh trực tiếp từ phím bấm cổng UART Serial
    while (1) {
        int c = getchar();

        if (c >= '1' && c <= '4') {
            int selected = c - '0';
            ESP_LOGI(TAG, "[SERIAL MANUAL] Chon Ngan %d", selected);
            bin_command_t cmd;
            cmd.bin_number = selected;
            strncpy(cmd.trash_label, "manual", sizeof(cmd.trash_label));
            strncpy(cmd.trash_display_name, "", sizeof(cmd.trash_display_name));
            xQueueSend(s_bin_cmd_queue, &cmd, 0);
        } else if (c == 'o' || c == 'O') {
            ESP_LOGI(TAG, "[SERIAL MANUAL] Mo nap 180 do");
            mg995_set_angle(180);
        } else if (c == 'c' || c == 'C') {
            ESP_LOGI(TAG, "[SERIAL MANUAL] Dong nap 0 do");
            mg995_set_angle(0);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

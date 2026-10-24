#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#include "wifi_config.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_wifi_connected = false;

// ====================================================================
// 1. CẤU HÌNH GPIO VÀ THÔNG SỐ SERVO & LCD 1602
// ====================================================================
#define SERVO_180_GPIO 19 // Servo 180 (MG995) hất rác (Chân G19 / D19)
#define SERVO_360_GPIO 18 // Servo 360 (MG996R) quay mâm 4 góc

#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 50000
#define LCD_I2C_ADDR 0x3F

#define LCD_BACKLIGHT 0x08
#define ENABLE 0x04
#define RS 0x01

// Thông số Servo Hất Rác
#define SERVO_180_REST_ANGLE 0 // Hằng số góc nghỉ cho API /servo

static int s_dump_rest_angle = 0; // Vị trí cũ / đóng ban đầu (độ)
static int s_dump_target_angle = 90; // Vị trí mở hất rác (độ)
static bool s_dump_inverted =
    false; // Cờ đảo chiều quay (false: 0->90->0, true: 180->90->180)
static int s_dump_fwd_time_ms = 500; // Thời gian lướt mở hất rác (ms)
static int s_dump_rev_time_ms = 500; // Thời gian lướt đóng nắp về cũ (ms)
static int s_dump_wait_ms = 1500; // Thời gian giữ mở để rác trượt xuống (ms)

#define SERVO_360_SPEED_FWD_US 1750 // Xung quay thuận (us)
#define SERVO_360_SPEED_REV_US 1250 // Xung quay nghịch (us)
#define SERVO_360_STOP_US 1500      // Xung dừng / phanh chủ động (us)

// Thời gian quay chuẩn
static int s_rotate_time_90_ms = 950;          // Thời gian quay đi 90 độ (Ngăn 2 thuận, Ngăn 4 nghịch)
static int s_rotate_time_90_rev_ms = 1010;     // Thời gian quay về 90 độ (về lại Home sau khi hất rác)
static int s_rotate_time_180_fwd_ms = 1850;
static int s_rotate_time_180_rev_ms = 1955;

static const char *TAG = "TRASHNET_ACTUATOR";
static int current_position = 0;
static int s_current_servo_angle = SERVO_180_REST_ANGLE;
static bool s_is_busy = false;
static char s_last_trash_name[32] = "San sang";

// ====================================================================
// LƯU TRỮ VÀ KHÔI PHỤC THÔNG SỐ HIỆU CHUẨN VÀO FLASH NVS (LƯU TRỰC TIẾP)
// ====================================================================
static void calib_save_to_nvs(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("act_calib", NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    nvs_set_i32(handle, "rot90", s_rotate_time_90_ms);
    nvs_set_i32(handle, "rot90r", s_rotate_time_90_rev_ms);
    nvs_set_i32(handle, "rot180f", s_rotate_time_180_fwd_ms);
    nvs_set_i32(handle, "rot180r", s_rotate_time_180_rev_ms);
    nvs_set_i32(handle, "dumpf", s_dump_fwd_time_ms);
    nvs_set_i32(handle, "dumpr", s_dump_rev_time_ms);
    nvs_set_i32(handle, "dumpw", s_dump_wait_ms);
    nvs_set_i32(handle, "dumprest", s_dump_rest_angle);
    nvs_set_i32(handle, "dumptgt", s_dump_target_angle);
    nvs_set_u8(handle, "inv", s_dump_inverted ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, ">>> [NVS FLASH] Da luu thanh cong thong so vao bo nho Flash!");
  } else {
    ESP_LOGE(TAG, ">>> [NVS FLASH] Khong the mo NVS de luu: %s", esp_err_to_name(err));
  }
}

static void calib_load_from_nvs(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("act_calib", NVS_READONLY, &handle);
  if (err == ESP_OK) {
    int32_t val;
    uint8_t u8val;
    if (nvs_get_i32(handle, "rot90", &val) == ESP_OK) s_rotate_time_90_ms = val;
    if (nvs_get_i32(handle, "rot90r", &val) == ESP_OK) s_rotate_time_90_rev_ms = val;
    if (nvs_get_i32(handle, "rot180f", &val) == ESP_OK) s_rotate_time_180_fwd_ms = val;
    if (nvs_get_i32(handle, "rot180r", &val) == ESP_OK) s_rotate_time_180_rev_ms = val;
    if (nvs_get_i32(handle, "dumpf", &val) == ESP_OK) s_dump_fwd_time_ms = val;
    if (nvs_get_i32(handle, "dumpr", &val) == ESP_OK) s_dump_rev_time_ms = val;
    if (nvs_get_i32(handle, "dumpw", &val) == ESP_OK) s_dump_wait_ms = val;
    if (nvs_get_u8(handle, "inv", &u8val) == ESP_OK) {
      s_dump_inverted = (u8val != 0);
    }
    if (nvs_get_i32(handle, "dumprest", &val) == ESP_OK) {
      s_dump_rest_angle = val;
    } else {
      s_dump_rest_angle = s_dump_inverted ? 180 : 0;
    }
    if (nvs_get_i32(handle, "dumptgt", &val) == ESP_OK) {
      s_dump_target_angle = val;
    } else {
      s_dump_target_angle = 90;
    }
    nvs_close(handle);
    ESP_LOGI(TAG, ">>> [NVS FLASH] Da tai thong so calib tu Flash NVS: rot90_fwd=%dms, rot90_rev=%dms, 180f=%dms, 180r=%dms, dumpf=%dms, dumpr=%dms, dumpw=%dms, rest_ang=%d, tgt_ang=%d, inv=%d",
             s_rotate_time_90_ms, s_rotate_time_90_rev_ms, s_rotate_time_180_fwd_ms, s_rotate_time_180_rev_ms,
             s_dump_fwd_time_ms, s_dump_rev_time_ms, s_dump_wait_ms, s_dump_rest_angle, s_dump_target_angle, s_dump_inverted ? 1 : 0);
  } else {
    s_dump_rest_angle = s_dump_inverted ? 180 : 0;
    s_dump_target_angle = 90;
    ESP_LOGI(TAG, ">>> [NVS FLASH] Chua co thong so da luu, dung mac dinh: rot90_fwd=%dms, rot90_rev=%dms, 180f=%dms, 180r=%dms",
             s_rotate_time_90_ms, s_rotate_time_90_rev_ms, s_rotate_time_180_fwd_ms, s_rotate_time_180_rev_ms);
  }
}

// Cấu trúc hàng đợi lệnh phân loại rác
// Các loại lệnh điều khiển phần cứng đưa vào hàng đợi tuần tự (không xung đột)
typedef enum {
  CMD_BIN_ACTION = 0, // Chu trình đầy đủ: Xoay 360 -> Hất rác 180 -> Về Home
  CMD_DUMP_ONLY,      // Chỉ hất rác 180 (Mở -> Chờ -> Đóng)
  CMD_SERVO_ANGLE,    // Đặt góc trực tiếp cho Servo 180
  CMD_SERVO_INVERT,   // Đảo chiều quay Servo 180
  CMD_SERVO_RAW_180,  // Quay Servo 180 theo hướng và thời gian ms
  CMD_ROTATE_3_FWD,   // Test chỉ quay đi Ngăn 3 (180 độ)
  CMD_ROTATE_3_REV,   // Test chỉ quay về Home từ Ngăn 3
  CMD_ROTATE_2_FWD,   // Test chỉ quay đi Ngăn 2 (+90 độ)
  CMD_ROTATE_2_REV,   // Test chỉ quay về Home từ Ngăn 2 (-90 độ)
  CMD_ROTATE_4_FWD,   // Test chỉ quay đi Ngăn 4 (-90 độ)
  CMD_ROTATE_4_REV    // Test chỉ quay về Home từ Ngăn 4 (+90 độ)
} cmd_type_t;

typedef struct {
  cmd_type_t type;
  int bin_number;              // 1 đến 4
  int param;                   // Tham số bổ trợ (ví dụ góc angle)
  char trash_label[32];        // Tên kỹ thuật (plastic, metal...)
  char trash_display_name[32]; // Tên hiển thị tiếng Việt
} bin_command_t;

static QueueHandle_t s_bin_cmd_queue = NULL;
static SemaphoreHandle_t s_servo_mutex = NULL; // Khóa bảo vệ PWM LEDC chống xung đột đa luồng

// ====================================================================
// 2. DRIVER LCD 1602 I2C
// ====================================================================
static bool s_lcd_present = true;

static void lcd_i2c_write_byte(uint8_t data) {
  if (!s_lcd_present)
    return;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (LCD_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, data, true);
  i2c_master_stop(cmd);
  i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 20 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
}

static void lcd_strobe(uint8_t data) {
  if (!s_lcd_present)
    return;
  lcd_i2c_write_byte(data | ENABLE | LCD_BACKLIGHT);
  esp_rom_delay_us(500);
  lcd_i2c_write_byte((data & ~ENABLE) | LCD_BACKLIGHT);
  esp_rom_delay_us(500);
}

static void lcd_write_4bits(uint8_t value, uint8_t mode) {
  if (!s_lcd_present)
    return;
  uint8_t pin_data = (value & 0xF0) | mode | LCD_BACKLIGHT;
  lcd_i2c_write_byte(pin_data);
  lcd_strobe(pin_data);
}

static void lcd_send_cmd(uint8_t cmd) {
  if (!s_lcd_present)
    return;
  lcd_write_4bits(cmd & 0xF0, 0);
  lcd_write_4bits((cmd << 4) & 0xF0, 0);
  if (cmd == 0x01 || cmd == 0x02) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void lcd_send_data(uint8_t data) {
  if (!s_lcd_present)
    return;
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

  vTaskDelay(pdMS_TO_TICKS(50));

  // Thử gửi lệnh kiểm tra LCD có phản hồi không
  i2c_cmd_handle_t check_cmd = i2c_cmd_link_create();
  i2c_master_start(check_cmd);
  i2c_master_write_byte(check_cmd, (LCD_I2C_ADDR << 1) | I2C_MASTER_WRITE,
                        true);
  i2c_master_stop(check_cmd);
  esp_err_t err =
      i2c_master_cmd_begin(I2C_MASTER_NUM, check_cmd, 30 / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(check_cmd);

  if (err != ESP_OK) {
    ESP_LOGW(TAG,
             "Khong tim thay LCD tai dia chi 0x%02X -> Bo qua LCD de chuong "
             "trinh va Servo chay nhanh, khong bi nghen!",
             LCD_I2C_ADDR);
    s_lcd_present = false;
    return;
  }

  s_lcd_present = true;
  lcd_write_4bits(0x30, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  lcd_write_4bits(0x30, 0);
  vTaskDelay(pdMS_TO_TICKS(5));
  lcd_write_4bits(0x30, 0);
  esp_rom_delay_us(1000);
  lcd_write_4bits(0x20, 0);
  esp_rom_delay_us(1000);

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
// 3. DRIVER SERVO 180 (GPIO 19) & SERVO 360 (GPIO 18)
// ====================================================================
static void servo_hardware_init(void) {
  ledc_timer_config_t timer_config = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .duty_resolution = LEDC_TIMER_16_BIT,
                                      .timer_num = LEDC_TIMER_0,
                                      .freq_hz = 50,
                                      .clk_cfg = LEDC_AUTO_CLK};
  ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

  // Kênh 0: Servo 180 (GPIO 19 / D19)
  ledc_channel_config_t ch180_config = {.gpio_num = SERVO_180_GPIO,
                                        .speed_mode = LEDC_LOW_SPEED_MODE,
                                        .channel = LEDC_CHANNEL_0,
                                        .timer_sel = LEDC_TIMER_0,
                                        .duty = 0,
                                        .hpoint = 0};
  ESP_ERROR_CHECK(ledc_channel_config(&ch180_config));

  // Kênh 1: Servo 360 (GPIO 18)
  ledc_channel_config_t ch360_config = {.gpio_num = SERVO_360_GPIO,
                                        .speed_mode = LEDC_LOW_SPEED_MODE,
                                        .channel = LEDC_CHANNEL_1,
                                        .timer_sel = LEDC_TIMER_0,
                                        .duty = 0,
                                        .hpoint = 0};
  ESP_ERROR_CHECK(ledc_channel_config(&ch360_config));
}

static inline uint32_t angle_to_pulse_us(int angle) {
  if (angle < 0)
    angle = 0;
  if (angle > 180)
    angle = 180;
  // Chuẩn MG995 / SG90: 500us (0 do) -> 2450us (180 do)
  return 500 + (angle * 1950) / 180;
}

static void servo_180_set_pulse_direct(uint32_t pulse_us) {
  uint32_t duty = (pulse_us * 65535) / 20000;
  if (s_servo_mutex) xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  if (s_servo_mutex) xSemaphoreGive(s_servo_mutex);
}

// Dừng / ngắt xung Servo hất rác: Ngắt duty = 0 để thả lỏng nghỉ ngơi, TUYỆT
// ĐỐI không gửi 1500us gây giật
static void servo_180_stop(void) {
  if (s_servo_mutex) xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  if (s_servo_mutex) xSemaphoreGive(s_servo_mutex);
}

// Điều khiển góc trực tiếp cho Servo 180 (0 - 180 độ)
static void servo_180_set_angle(int angle) {
  if (angle < 0)
    angle = 0;
  if (angle > 180)
    angle = 180;
  s_current_servo_angle = angle;
  servo_180_set_pulse_direct(angle_to_pulse_us(angle));
  ESP_LOGI(TAG, "[SERVO 180 - G%d] Dat goc %d do", SERVO_180_GPIO, angle);
}

// Dừng Servo 360: Sử dụng phanh chủ động bằng xung 1500us trước, sau đó ngắt
// duty để chống trôi hoàn toàn
static void servo_360_stop(void) {
  // 1. Phanh điện động 1500us để triệt tiêu quán tính quay (tránh bị quay
  // lố/séo góc)
  uint32_t duty_stop = (SERVO_360_STOP_US * 65535) / 20000;
  if (s_servo_mutex) xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty_stop);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  if (s_servo_mutex) xSemaphoreGive(s_servo_mutex);
  vTaskDelay(pdMS_TO_TICKS(60));

  // 2. Ngắt hẳn xung PWM để mô-tơ đứng yên tuyệt đối, không rung giật
  if (s_servo_mutex) xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  if (s_servo_mutex) xSemaphoreGive(s_servo_mutex);
}

// Quay Servo 360 theo chiều (dir = 1: thuận, dir = -1: nghịch) trong
// duration_ms
static void servo_360_rotate_raw(int dir, int duration_ms) {
  if (duration_ms <= 0)
    return;

  uint32_t pulse_us = SERVO_360_STOP_US;
  if (dir > 0) {
    pulse_us = SERVO_360_SPEED_FWD_US;
  } else if (dir < 0) {
    pulse_us = SERVO_360_SPEED_REV_US;
  } else {
    servo_360_stop();
    return;
  }

  uint32_t duty = (pulse_us * 65535) / 20000;
  if (s_servo_mutex) xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  if (s_servo_mutex) xSemaphoreGive(s_servo_mutex);

  // Sử dụng Hardware Timer (esp_timer_get_time) chính xác đến microsecond,
  // loại bỏ hoàn toàn sai số làm tròn FreeRTOS Tick (10ms) và độ trễ task
  int64_t target_us = (int64_t)duration_ms * 1000;
  int64_t start_us = esp_timer_get_time();

  while (1) {
    int64_t elapsed_us = esp_timer_get_time() - start_us;
    int64_t remaining_us = target_us - elapsed_us;
    if (remaining_us <= 0) {
      break;
    }
    if (remaining_us > 25000) {
      // Khi thời gian còn nhiều, nhường CPU cho các task khác
      vTaskDelay(pdMS_TO_TICKS(15));
    } else {
      // Khi còn < 25ms cuối, dùng hardware busy-wait để đạt độ chính xác tuyệt đối từng ms
      if (remaining_us > 0) {
        esp_rom_delay_us((uint32_t)remaining_us);
      }
      break;
    }
  }

  servo_360_stop();
}

// Di chuyển mượt mà từ from_angle đến to_angle trong duration_ms
// Chia nhỏ bước mỗi 20ms tương ứng tần số 50Hz giúp servo quay êm ru, không bị
// giật nảy
static void servo_180_move_smooth(int from_angle, int to_angle,
                                  int duration_ms) {
  if (duration_ms <= 0) {
    servo_180_set_angle(to_angle);
    return;
  }

  const int step_delay_ms = 20;
  int total_steps = duration_ms / step_delay_ms;
  if (total_steps < 1)
    total_steps = 1;

  for (int i = 1; i <= total_steps; i++) {
    int cur_angle = from_angle + ((to_angle - from_angle) * i) / total_steps;
    servo_180_set_pulse_direct(angle_to_pulse_us(cur_angle));
    s_current_servo_angle = cur_angle;
    vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
  }

  servo_180_set_pulse_direct(angle_to_pulse_us(to_angle));
  s_current_servo_angle = to_angle;
}

// Đảo chiều quay cho Servo hất rác (phục vụ người dùng lắp servo theo hướng
// ngược lại)
static void servo_180_toggle_direction(void) {
  s_dump_inverted = !s_dump_inverted;
  if (s_dump_inverted) {
    s_dump_rest_angle = 180;
    s_dump_target_angle = 90;
    ESP_LOGW(TAG, ">>> [SERVO HAT RAC] DA DAO SANG CHIEU NGUOC! (Nghi: 180 do "
                  "-> Mo: 90 do -> Dong ve: 180 do)");
  } else {
    s_dump_rest_angle = 0;
    s_dump_target_angle = 90;
    ESP_LOGW(TAG, ">>> [SERVO HAT RAC] DA CHUYEN VE CHIEU THUAN! (Nghi: 0 do "
                  "-> Mo: 90 do -> Dong ve: 0 do)");
  }
  servo_180_set_angle(s_dump_rest_angle);
  vTaskDelay(pdMS_TO_TICKS(300));
  servo_180_stop();
}

// Quay Servo hất rác theo chiều và thời gian ms (lướt êm, không giật)
// dir = 1: Quay mở nắp hất rác
// dir = -1: Quay đóng nắp về vị trí cũ
static void servo_180_rotate_raw(int dir, int duration_ms) {
  if (duration_ms <= 0)
    return;

  if (dir > 0) {
    ESP_LOGI(TAG, "[SERVO HAT RAC G%d] Quay MO (%d ms): tu %d do sang %d do",
             SERVO_180_GPIO, duration_ms, s_dump_rest_angle,
             s_dump_target_angle);
    servo_180_move_smooth(s_dump_rest_angle, s_dump_target_angle, duration_ms);
  } else if (dir < 0) {
    ESP_LOGI(TAG, "[SERVO HAT RAC G%d] Quay DONG (%d ms): tu %d do ve %d do",
             SERVO_180_GPIO, duration_ms, s_dump_target_angle,
             s_dump_rest_angle);
    servo_180_move_smooth(s_dump_target_angle, s_dump_rest_angle, duration_ms);
    vTaskDelay(pdMS_TO_TICKS(200));
    servo_180_stop();
  } else {
    servo_180_stop();
  }
}

// Chu trình hất rác êm ái:
// 1. Lướt êm từ vị trí cũ -> mở hất rác trong s_dump_fwd_time_ms
// 2. GIỮ CHẮC nắp mở trong s_dump_wait_ms để rác trượt xuống (không ngắt xung
// làm sập nắp)
// 3. Lướt êm từ vị trí hất rác -> quay về vị trí cũ trong s_dump_rev_time_ms
// 4. Định vị chắc chắn tại vị trí cũ rồi mới thả lỏng ngắt duty = 0
static void servo_180_dump_trash(void) {
  int start_pos = s_dump_rest_angle;
  int dump_pos = s_dump_target_angle;

  ESP_LOGI(TAG,
           ">>> [SERVO HAT RAC] 1. Bat dau quay mo tu %d do sang %d do (chay "
           "em trong %d ms)",
           start_pos, dump_pos, s_dump_fwd_time_ms);
  servo_180_move_smooth(start_pos, dump_pos, s_dump_fwd_time_ms);

  ESP_LOGI(
      TAG,
      ">>> [SERVO HAT RAC] 2. GIU NGUYEN vi tri %d do cho rac roi (%d ms)...",
      dump_pos, s_dump_wait_ms);
  vTaskDelay(pdMS_TO_TICKS(s_dump_wait_ms));

  ESP_LOGI(TAG,
           ">>> [SERVO HAT RAC] 3. Quay dong ve lai vi tri cu tu %d do ve %d "
           "do (chay em trong %d ms)",
           dump_pos, start_pos, s_dump_rev_time_ms);
  servo_180_move_smooth(dump_pos, start_pos, s_dump_rev_time_ms);

  vTaskDelay(pdMS_TO_TICKS(200));
  servo_180_stop();
  ESP_LOGI(TAG,
           ">>> [SERVO HAT RAC] Da ve lai vi tri cu %d do va tha long nghi "
           "ngoi hoan tat!",
           start_pos);
}

// Khởi tạo toàn bộ phần cứng
static void hardware_init(void) {
  if (s_servo_mutex == NULL) {
    s_servo_mutex = xSemaphoreCreateMutex();
  }
  servo_hardware_init();
  servo_360_stop();
  servo_180_stop(); // Không ép servo gồng ngay lúc vừa bật nguồn để chống sụt
                    // áp

  lcd_init();
  ESP_LOGI(TAG, "Hardware Initialized (Servo 360 [G18] + Servo 180 [G19 / D19] "
                "+ LCD 1602)!");
}

// ====================================================================
// 4. CHU TRÌNH PHÂN LOẠI RÁC 4 GÓC (ĐỒNG BỘ: XOAY 360 -> HẤT 180 -> VỀ HOME)
// ====================================================================
static void process_bin_action(int bin_number, const char *display_name) {
  const char *trash_categories[4] = {
      "CHAY DUOC", // Ngăn 1: Rác Cháy Được (burnable, biological)
      "THUY TINH", // Ngăn 2: Thủy Tinh (glass)
      "KIM LOAI",  // Ngăn 3: Kim Loại (metal)
      "NHUA (PET)" // Ngăn 4: Nhựa & Chai PET (plastic)
  };

  if (bin_number < 1 || bin_number > 4)
    return;
  s_is_busy = true;

  int target_idx = bin_number - 1;
  const char *category = trash_categories[target_idx];

  // Dòng 1 LCD: "NGAN 1: CHAY DUO" (16 ký tự)
  char line1[32];
  snprintf(line1, sizeof(line1), "NGAN %d: %-8s", bin_number, category);

  // Dòng 2 LCD: Tên chi tiết rác (16 ký tự)
  char line2[32];
  if (display_name && strlen(display_name) > 0) {
    snprintf(line2, sizeof(line2), "%-16.16s", display_name);
  } else {
    snprintf(line2, sizeof(line2), "Rac %-12.12s", category);
  }

  strncpy(s_last_trash_name, line2, sizeof(s_last_trash_name) - 1);
  s_last_trash_name[sizeof(s_last_trash_name) - 1] = '\0';

  ESP_LOGI(TAG, "==================================================");
  ESP_LOGI(TAG, ">>> BAT DAU CHU TRINH DONG BO: Ngan %d [%s] - %s", bin_number,
           category, line2);

  // 1. Hiển thị thông tin nhận diện lên LCD 1602
  lcd_display_lines(line1, line2);
  vTaskDelay(pdMS_TO_TICKS(1000));

  // 2. Tính toán hướng và thời gian quay của Servo 360
  // Ngăn 1: Vị trí Home (0 độ, không cần quay)
  // Ngăn 2: Quay thuận 90 độ
  // Ngăn 3: Quay thuận 180 độ (sử dụng thời gian đi và về độc lập để không bị
  // lệch) Ngăn 4: Quay nghịch 90 độ (đường ngắn nhất)
  int dir = 0;
  int duration_go = 0;
  int duration_ret = 0;

  if (target_idx == 0) {
    dir = 0;
    duration_go = 0;
    duration_ret = 0;
  } else if (target_idx == 1) {
    dir = 1;
    duration_go = s_rotate_time_90_ms;
    duration_ret = s_rotate_time_90_rev_ms;
  } else if (target_idx == 2) {
    dir = 1;
    duration_go = s_rotate_time_180_fwd_ms;
    duration_ret = s_rotate_time_180_rev_ms;
  } else if (target_idx == 3) {
    dir = -1;
    duration_go = s_rotate_time_90_ms;
    duration_ret = s_rotate_time_90_rev_ms;
  }

  // 3. Servo 360 quay từ Home đến góc thùng rác tương ứng
  if (duration_go > 0) {
    current_position = target_idx;
    char rotate_msg[32];
    snprintf(rotate_msg, sizeof(rotate_msg), "XOAY DEN NGAN %d", bin_number);
    lcd_display_lines(line1, rotate_msg);

    ESP_LOGI(TAG, ">>> [1/3] Servo 360: Quay %s den Ngan %d (%d ms)",
             dir > 0 ? "thuan" : "nghich", bin_number, duration_go);
    servo_360_rotate_raw(dir, duration_go);
    vTaskDelay(pdMS_TO_TICKS(400)); // Chờ thùng ổn định
  } else {
    current_position = 0;
    ESP_LOGI(TAG,
             ">>> [1/3] Ngan 1 o ngay vi tri Home, khong can quay Servo 360");
  }

  // 4. Servo 180 hất rác xuống (180 độ -> 90 độ -> đợi rác rơi -> 180 độ)
  lcd_display_lines(line1, "DANG HAT RAC...");
  ESP_LOGI(TAG, ">>> [2/3] Servo 180: Bat dau hat rac xuong");
  servo_180_dump_trash();

  // 5. Servo 360 quay NGƯỢC LẠI để về đúng vị trí Home ban đầu (chống xoắn rối
  // dây)
  if (duration_ret > 0) {
    lcd_display_lines(line1, "DANG VE VI TRI..");
    ESP_LOGI(TAG,
             ">>> [3/3] Servo 360: Quay nguoc %s VE LAI HOME (%d ms) (tranh "
             "roi day)",
             (-dir) > 0 ? "thuan" : "nghich", duration_ret);
    servo_360_rotate_raw(-dir, duration_ret);
    vTaskDelay(pdMS_TO_TICKS(400));
  }
  current_position = 0; // Đã về vị trí Home ban đầu an toàn

  // 6. Hoàn tất chu trình, hiển thị sẵn sàng nhận rác tiếp theo
  lcd_show_ready();
  s_is_busy = false;
  ESP_LOGI(TAG, ">>> HOAN TAT CHU TRINH NGAN %d! He thong da ve lai vi tri cu.",
           bin_number);
  ESP_LOGI(TAG, "==================================================");
}

// Task chạy ngầm tuần tự hóa mọi lệnh phần cứng (chống xung đột PWM đa luồng & chống treo Servo 180)
static void actuator_worker_task(void *pvParameters) {
  bin_command_t cmd;
  while (1) {
    if (xQueueReceive(s_bin_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
      s_is_busy = true;
      switch (cmd.type) {
        case CMD_BIN_ACTION:
          process_bin_action(cmd.bin_number, cmd.trash_display_name);
          break;
        case CMD_DUMP_ONLY:
          ESP_LOGI(TAG, ">>> [WORKER] Chay lenh Dump Only (Hat rac 180)");
          servo_180_dump_trash();
          break;
        case CMD_SERVO_ANGLE:
          ESP_LOGI(TAG, ">>> [WORKER] Dat goc Servo 180: %d do", cmd.param);
          servo_180_set_angle(cmd.param);
          break;
        case CMD_SERVO_INVERT:
          ESP_LOGI(TAG, ">>> [WORKER] Dao chieu quay Servo 180");
          servo_180_toggle_direction();
          calib_save_to_nvs();
          break;
        case CMD_SERVO_RAW_180:
          ESP_LOGI(TAG, ">>> [WORKER] Quay Servo 180 dir=%d, time=%d ms", cmd.bin_number, cmd.param);
          servo_180_rotate_raw(cmd.bin_number, cmd.param);
          break;
        case CMD_ROTATE_3_FWD:
          ESP_LOGI(TAG, ">>> [WORKER] Test CHI QUAY DI Ngan 3 (%d ms)", s_rotate_time_180_fwd_ms);
          servo_360_rotate_raw(1, s_rotate_time_180_fwd_ms);
          break;
        case CMD_ROTATE_3_REV:
          ESP_LOGI(TAG, ">>> [WORKER] Test CHI QUAY VE Home tu Ngan 3 (%d ms)", s_rotate_time_180_rev_ms);
          servo_360_rotate_raw(-1, s_rotate_time_180_rev_ms);
          break;
        case CMD_ROTATE_2_FWD:
          ESP_LOGI(TAG, ">>> [WORKER] Test CHI QUAY DI Ngan 2 (+90 do) (%d ms)", s_rotate_time_90_ms);
          servo_360_rotate_raw(1, s_rotate_time_90_ms);
          break;
        case CMD_ROTATE_2_REV:
          ESP_LOGI(TAG, ">>> [WORKER] Test CHI QUAY VE Home tu Ngan 2 (%d ms)", s_rotate_time_90_rev_ms);
          servo_360_rotate_raw(-1, s_rotate_time_90_rev_ms);
          break;
        case CMD_ROTATE_4_FWD:
          ESP_LOGI(TAG, ">>> [WORKER] Test CHI QUAY DI Ngan 4 (-90 do) (%d ms)", s_rotate_time_90_ms);
          servo_360_rotate_raw(-1, s_rotate_time_90_ms);
          break;
        case CMD_ROTATE_4_REV:
          ESP_LOGI(TAG, ">>> [WORKER] Test CHI QUAY VE Home tu Ngan 4 (%d ms)", s_rotate_time_90_rev_ms);
          servo_360_rotate_raw(1, s_rotate_time_90_rev_ms);
          break;
        default:
          ESP_LOGW(TAG, ">>> [WORKER] Lenh khong xac dinh (cmd.type=%d)", cmd.type);
          break;
      }
      s_is_busy = false;
    }
  }
}

// ====================================================================
// 5. HTTP SERVER ĐIỀU KHIỂN QUA MẠNG (TỪ WEB APP / FLASK)
// ====================================================================

// Giải mã URL (%20 -> khoảng trắng, loại bỏ ký tự ngoài ASCII)
static void url_decode(char *dst, const char *src, size_t dst_size) {
  if (!dst || !src || dst_size == 0)
    return;
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

  if (bin < 1)
    bin = 1;
  if (bin > 4)
    bin = 4;

  bin_command_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_BIN_ACTION;
  cmd.bin_number = bin;
  strncpy(cmd.trash_label, label, sizeof(cmd.trash_label) - 1);
  cmd.trash_label[sizeof(cmd.trash_label) - 1] = '\0';
  strncpy(cmd.trash_display_name, (strlen(name) > 0) ? name : label,
          sizeof(cmd.trash_display_name) - 1);
  cmd.trash_display_name[sizeof(cmd.trash_display_name) - 1] = '\0';

  // Đẩy vào hàng đợi để task ngầm xử lý tuần tự (không chặn HTTP task)
  xQueueSend(s_bin_cmd_queue, &cmd, 0);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Connection", "close");

  char resp[160];
  snprintf(resp, sizeof(resp),
           "{\"status\":\"success\",\"bin\":%d,\"label\":\"%s\",\"message\":"
           "\"Executing bin action\"}",
           bin, label);
  return httpd_resp_send(req, resp, strlen(resp));
}

// GET /status: Trả về trạng thái hiện tại của thùng rác cho Web UI (phản hồi siêu nhanh < 1ms)
static esp_err_t http_status_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Connection", "close");

  char resp[450];
  snprintf(resp, sizeof(resp),
           "{\"online\":true,\"current_bin\":%d,\"current_angle\":%d,\"servo_angle\":%d,"
           "\"rotate_time_ms\":%d,\"rotate_90_ms\":%d,\"rotate_90_rev_ms\":%d,\"rotate_180_fwd_ms\":%d,\"rotate_180_rev_ms\":%d,"
           "\"dump_fwd_ms\":%d,\"dump_rev_ms\":%d,\"dump_wait_ms\":%d,\"inverted\":%s,"
           "\"rest_angle\":%d,\"dump_angle\":%d,\"is_busy\":%s,\"last_trash\":\"%s\"}",
           current_position + 1, current_position * 90, s_current_servo_angle,
           s_rotate_time_90_ms, s_rotate_time_90_ms, s_rotate_time_90_rev_ms, s_rotate_time_180_fwd_ms, s_rotate_time_180_rev_ms,
           s_dump_fwd_time_ms, s_dump_rev_time_ms, s_dump_wait_ms,
           s_dump_inverted ? "true" : "false", s_dump_rest_angle,
           s_dump_target_angle, s_is_busy ? "true" : "false",
           s_last_trash_name);
  return httpd_resp_send(req, resp, strlen(resp));
}

// GET /calib?t90=950&t90r=1010&t180f=1850&t180r=1955&dumpf=500&dumpr=500&dumpw=1500&inv=0
static esp_err_t http_calib_handler(httpd_req_t *req) {
  char query[256] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char val[32];
    if (httpd_query_key_value(query, "t90", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 50 && v <= 10000) s_rotate_time_90_ms = v;
    }
    if (httpd_query_key_value(query, "t90r", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "rot90r", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 50 && v <= 10000) s_rotate_time_90_rev_ms = v;
    }
    if (httpd_query_key_value(query, "t180f", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 50 && v <= 10000) s_rotate_time_180_fwd_ms = v;
    }
    if (httpd_query_key_value(query, "t180r", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 50 && v <= 10000) s_rotate_time_180_rev_ms = v;
    }
    if (httpd_query_key_value(query, "dumpf", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 50 && v <= 10000) s_dump_fwd_time_ms = v;
    }
    if (httpd_query_key_value(query, "dumpr", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 50 && v <= 10000) s_dump_rev_time_ms = v;
    }
    if (httpd_query_key_value(query, "dumpw", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 100 && v <= 10000) s_dump_wait_ms = v;
    }
    if (httpd_query_key_value(query, "inv", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      s_dump_inverted = (v != 0);
      s_dump_rest_angle = s_dump_inverted ? 180 : 0;
      s_dump_target_angle = 90;
    }
    if (httpd_query_key_value(query, "dumprest", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "rest_ang", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 0 && v <= 180) s_dump_rest_angle = v;
    }
    if (httpd_query_key_value(query, "dumptgt", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "dump_ang", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 0 && v <= 180) s_dump_target_angle = v;
    }
    calib_save_to_nvs();
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Connection", "close");
  char resp[450];
  snprintf(resp, sizeof(resp),
           "{\"status\":\"ok\",\"rotate_90_ms\":%d,\"rotate_90_rev_ms\":%d,\"rotate_180_fwd_ms\":%d,\"rotate_180_rev_ms\":%d,"
           "\"dump_fwd_ms\":%d,\"dump_rev_ms\":%d,\"dump_wait_ms\":%d,\"rest_angle\":%d,\"dump_angle\":%d,\"inverted\":%s}",
           s_rotate_time_90_ms, s_rotate_time_90_rev_ms, s_rotate_time_180_fwd_ms, s_rotate_time_180_rev_ms,
           s_dump_fwd_time_ms, s_dump_rev_time_ms, s_dump_wait_ms,
           s_dump_rest_angle, s_dump_target_angle,
           s_dump_inverted ? "true" : "false");
  return httpd_resp_send(req, resp, strlen(resp));
}

// GET /servo?dump=1 (hoặc ?invert=1 hoặc ?fwd=500 hoặc ?rev=500 hoặc ?time=500
// hoặc ?angle=90): Test hất rác BẤT ĐỒNG BỘ qua Queue (không chặn httpd!)
static esp_err_t http_servo_handler(httpd_req_t *req) {
  char query[64] = {0};

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char val[16];
    if (httpd_query_key_value(query, "dump", val, sizeof(val)) == ESP_OK) {
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_DUMP_ONLY;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (httpd_query_key_value(query, "invert", val, sizeof(val)) == ESP_OK) {
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_SERVO_INVERT;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (httpd_query_key_value(query, "fwd", val, sizeof(val)) == ESP_OK) {
      int ms = atoi(val);
      if (ms <= 0) ms = s_dump_fwd_time_ms;
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_SERVO_RAW_180;
      cmd.bin_number = 1;
      cmd.param = ms;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (httpd_query_key_value(query, "rev", val, sizeof(val)) == ESP_OK) {
      int ms = atoi(val);
      if (ms <= 0) ms = s_dump_rev_time_ms;
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_SERVO_RAW_180;
      cmd.bin_number = -1;
      cmd.param = ms;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (httpd_query_key_value(query, "fwd_time", val, sizeof(val)) == ESP_OK) {
      int ms = atoi(val);
      if (ms >= 50 && ms <= 5000) {
        s_dump_fwd_time_ms = ms;
        calib_save_to_nvs();
      }
    } else if (httpd_query_key_value(query, "rev_time", val, sizeof(val)) == ESP_OK) {
      int ms = atoi(val);
      if (ms >= 50 && ms <= 5000) {
        s_dump_rev_time_ms = ms;
        calib_save_to_nvs();
      }
    } else if (httpd_query_key_value(query, "time", val, sizeof(val)) == ESP_OK) {
      int ms = atoi(val);
      if (ms >= 50 && ms <= 5000) {
        s_dump_fwd_time_ms = ms;
        s_dump_rev_time_ms = ms;
        calib_save_to_nvs();
      }
    } else if (httpd_query_key_value(query, "angle", val, sizeof(val)) == ESP_OK) {
      int angle = atoi(val);
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_SERVO_ANGLE;
      cmd.param = angle;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Connection", "close");
  char resp[140];
  snprintf(resp, sizeof(resp),
           "{\"status\":\"ok\",\"dump_fwd_ms\":%d,\"dump_rev_ms\":%d,\"inverted\":%s}",
           s_dump_fwd_time_ms, s_dump_rev_time_ms, s_dump_inverted ? "true" : "false");
  return httpd_resp_send(req, resp, strlen(resp));
}

// GET /rotate?bin=1..4 hoặc ?test=3_fwd / 3_rev / 2_fwd / 2_rev / 4_fwd / 4_rev: Test quay BẤT ĐỒNG BỘ qua Queue
static esp_err_t http_rotate_handler(httpd_req_t *req) {
  char query[64] = {0};
  int bin = 1;
  bool handled = false;

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char test_val[16];
    if (httpd_query_key_value(query, "test", test_val, sizeof(test_val)) == ESP_OK) {
      if (strcmp(test_val, "3_fwd") == 0 || strcmp(test_val, "fwd180") == 0) {
        ESP_LOGI(TAG, "[API /rotate] Queue Test CHI QUAY DI Ngan 3 (180 do): %d ms", s_rotate_time_180_fwd_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_3_FWD;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
        handled = true;
      } else if (strcmp(test_val, "3_rev") == 0 || strcmp(test_val, "rev180") == 0) {
        ESP_LOGI(TAG, "[API /rotate] Queue Test CHI QUAY VE Home tu Ngan 3: %d ms", s_rotate_time_180_rev_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_3_REV;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
        handled = true;
      } else if (strcmp(test_val, "2_fwd") == 0 || strcmp(test_val, "fwd90") == 0) {
        ESP_LOGI(TAG, "[API /rotate] Queue Test CHI QUAY DI Ngan 2 (+90 do): %d ms", s_rotate_time_90_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_2_FWD;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
        handled = true;
      } else if (strcmp(test_val, "2_rev") == 0 || strcmp(test_val, "rev90") == 0) {
        ESP_LOGI(TAG, "[API /rotate] Queue Test CHI QUAY VE Home tu Ngan 2 (-90 do): %d ms", s_rotate_time_90_rev_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_2_REV;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
        handled = true;
      } else if (strcmp(test_val, "4_fwd") == 0) {
        ESP_LOGI(TAG, "[API /rotate] Queue Test CHI QUAY DI Ngan 4 (-90 do): %d ms", s_rotate_time_90_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_4_FWD;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
        handled = true;
      } else if (strcmp(test_val, "4_rev") == 0) {
        ESP_LOGI(TAG, "[API /rotate] Queue Test CHI QUAY VE Home tu Ngan 4 (+90 do): %d ms", s_rotate_time_90_rev_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_4_REV;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
        handled = true;
      }
    }
    
    if (!handled) {
      char val[16];
      if (httpd_query_key_value(query, "bin", val, sizeof(val)) == ESP_OK) {
        bin = atoi(val);
      }
      if (bin >= 1 && bin <= 4) {
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_BIN_ACTION;
        cmd.bin_number = bin;
        strncpy(cmd.trash_label, "manual_test", sizeof(cmd.trash_label) - 1);
        strncpy(cmd.trash_display_name, "Test quay", sizeof(cmd.trash_display_name) - 1);
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
        handled = true;
      }
    }
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Connection", "close");
  char resp[180];
  snprintf(resp, sizeof(resp), 
           "{\"current_bin\":%d,\"status\":\"ok\",\"rotate_90_ms\":%d,\"rotate_90_rev_ms\":%d,\"rotate_180_fwd_ms\":%d,\"rotate_180_rev_ms\":%d}",
           bin, s_rotate_time_90_ms, s_rotate_time_90_rev_ms, s_rotate_time_180_fwd_ms, s_rotate_time_180_rev_ms);
  return httpd_resp_send(req, resp, strlen(resp));
}


static httpd_handle_t start_webserver(void) {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_open_sockets = 7;
  config.lru_purge_enable = true;
  config.stack_size = 8192;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t action_uri = {.uri = "/action",
                              .method = HTTP_GET,
                              .handler = http_action_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &action_uri);

    httpd_uri_t status_uri = {.uri = "/status",
                              .method = HTTP_GET,
                              .handler = http_status_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &status_uri);

    httpd_uri_t calib_uri = {.uri = "/calib",
                             .method = HTTP_GET,
                             .handler = http_calib_handler,
                             .user_ctx = NULL};
    httpd_register_uri_handler(server, &calib_uri);

    httpd_uri_t servo_uri = {.uri = "/servo",
                             .method = HTTP_GET,
                             .handler = http_servo_handler,
                             .user_ctx = NULL};
    httpd_register_uri_handler(server, &servo_uri);

    httpd_uri_t rotate_uri = {.uri = "/rotate",
                              .method = HTTP_GET,
                              .handler = http_rotate_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &rotate_uri);

    ESP_LOGI(TAG, "HTTP Server da khoi dong tren port 80!");
  }
  return server;
}

// ====================================================================
// 6. XỬ LÝ SỰ KIỆN WIFI
// ====================================================================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_is_wifi_connected = false;
    if (s_retry_num < 10) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGW(TAG, "Dang thu ket noi lai WiFi lan %d...", s_retry_num);
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(
        TAG,
        "\n\n"
        "*****************************************************************\n"
        "**                                                             **\n"
        "**  >>> IP ESP32 ACTUATOR (SERVO 360 [G18] + SERVO 180 [G19]): **\n"
        "**  >>> http://" IPSTR "                                       **\n"
        "**  >>> DAN DIA CHI NAY VAO WEB TRASHNET DE DIEU KHIEN         **\n"
        "**                                                             **\n"
        "*****************************************************************\n",
        IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    s_is_wifi_connected = true;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    // Hiển thị IP lên LCD lúc khởi động thành công
    char ip_str[32];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
    lcd_display_lines("WIFI DA KET NOI", ip_str);
    vTaskDelay(pdMS_TO_TICKS(1800));

    lcd_show_ready();
  }
}

static void wifi_init_sta(void) {
  s_wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = CONFIG_ESP_WIFI_SSID,
              .password = CONFIG_ESP_WIFI_PASSWORD,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WiFi STA khoi tao hoan tat. Dang ket noi toi SSID: %s",
           CONFIG_ESP_WIFI_SSID);
}

// ====================================================================
// 7. HÀM MAIN CHÍNH
// ====================================================================
void app_main(void) {
  // 0. Vô hiệu hóa Brownout Detector để chống sụt áp nguồn USB làm ESP32 reset
  // liên tục
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // 1. Khởi tạo Flash NVS (cho WiFi)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // 2. Khởi tạo Phần cứng (Servo 180 [G19 / D19], Servo 360 [G18], LCD 1602)
  hardware_init();

  // Đọc thông số hiệu chuẩn đã lưu từ bộ nhớ Flash NVS
  calib_load_from_nvs();

  // Màn hình khởi động
  char wifi_buf[32];
  snprintf(wifi_buf, sizeof(wifi_buf), "%-16.16s", CONFIG_ESP_WIFI_SSID);
  lcd_display_lines("DANG KET NOI...", wifi_buf);

  // 3. Khởi tạo hàng đợi lệnh và Task xử lý ngầm
  s_bin_cmd_queue = xQueueCreate(10, sizeof(bin_command_t));
  xTaskCreate(actuator_worker_task, "actuator_worker", 4096, NULL, 5, NULL);

  // 4. Kết nối WiFi
  wifi_init_sta();

  // 5. Khởi động Web Server HTTP
  start_webserver();

  ESP_LOGI(TAG,
           "=================================================================");
  ESP_LOGI(TAG, "=== ESP32 ACTUATOR - HE THONG SAN SANG ===");
  ESP_LOGI(TAG,
           "Phim test chu trinh DONG BO (Xoay 360 -> Hat rac ms -> Ve Home):");
  ESP_LOGI(
      TAG,
      "  Phim 1 : Chu trinh Ngan 1 (tai cho: hat rac theo ms -> ve vi tri cu)");
  ESP_LOGI(TAG, "  Phim 2 : Chu trinh Ngan 2 (quay 360 thuan 90 do -> hat rac "
                "-> ve Home)");
  ESP_LOGI(TAG, "  Phim 3 : Chu trinh Ngan 3 (quay 360 thuan 180 do -> hat rac "
                "-> ve Home)");
  ESP_LOGI(TAG, "  Phim 4 : Chu trinh Ngan 4 (quay 360 nghich 90 do -> hat rac "
                "-> ve Home)");
  ESP_LOGI(TAG,
           "  Phim f : Tuong tu phim 2 (quay thuan 90 do -> hat -> ve Home)");
  ESP_LOGI(TAG,
           "  Phim r : Tuong tu phim 4 (quay nghich 90 do -> hat -> ve Home)");
  ESP_LOGI(TAG,
           "-----------------------------------------------------------------");
  ESP_LOGI(
      TAG,
      "Phim TEST SERVO HAT RAC (G19 / D19 - Chay em theo ms, Khong giat):");
  ESP_LOGI(
      TAG,
      "  Phim t : Test chu trinh hat rac (Mo %d ms -> Doi %d ms -> Dong %d ms)",
      s_dump_fwd_time_ms, s_dump_wait_ms, s_dump_rev_time_ms);
  ESP_LOGI(TAG, "  Phim o : Test quay MO hat rac (%d do sang %d do, %d ms)",
           s_dump_rest_angle, s_dump_target_angle, s_dump_fwd_time_ms);
  ESP_LOGI(TAG, "  Phim c : Test quay DONG ve vi tri cu (%d do sang %d do, %d ms)",
           s_dump_target_angle, s_dump_rest_angle, s_dump_rev_time_ms);
  ESP_LOGI(TAG,
           "  Phim i : DAO CHIEU QUAY (Chuyen giua [0->90->0] va "
           "[180->90->180], Hien tai: %s)",
           s_dump_inverted ? "CHIEU NGUOC" : "CHIEU THUAN");
  ESP_LOGI(TAG, "  🎯 CAN CHINH GOC DONG (CHONG BI LO KHI VE 0 DO):");
  ESP_LOGI(TAG,
           "    Phim . hoac > : TANG goc dong nghi (+1 do / +5 do, Hien tai: %d do, Tu dong luu NVS)",
           s_dump_rest_angle);
  ESP_LOGI(TAG,
           "    Phim , hoac < : GIAM goc dong nghi (-1 do / -5 do, Hien tai: %d do, Tu dong luu NVS)",
           s_dump_rest_angle);
  ESP_LOGI(TAG, "  ⚡ CAN CHINH THOI GIAN LUOT (TOC DO CHAY EM):");
  ESP_LOGI(TAG,
           "    Phim [ hoac { : GIAM thoi gian QUAY MO (-5 ms, Hien tai: %d ms, Tu dong luu NVS)",
           s_dump_fwd_time_ms);
  ESP_LOGI(TAG,
           "    Phim ] hoac } : TANG thoi gian QUAY MO (+5 ms, Hien tai: %d ms, Tu dong luu NVS)",
           s_dump_fwd_time_ms);
  ESP_LOGI(TAG,
           "    Phim ; hoac : : GIAM thoi gian QUAY DONG (-5 ms, Hien tai: %d ms, Tu dong luu NVS)",
           s_dump_rev_time_ms);
  ESP_LOGI(TAG,
           "    Phim ' hoac \" : TANG thoi gian QUAY DONG (+5 ms, Hien tai: %d ms, Tu dong luu NVS)",
           s_dump_rev_time_ms);
  ESP_LOGI(TAG, "  Phim x : Dung / ngat xung PWM Servo hat rac (tha long)");
  ESP_LOGI(TAG,
           "-----------------------------------------------------------------");
  ESP_LOGI(TAG, "🔥 CAN CHINH RIENG BIET NGAN 3 (180 DO - KIM LOAI) (±5ms, Tu dong luu Flash NVS):");
  ESP_LOGI(TAG, "  Phim 3        : Chay TRON CHU TRINH Ngan 3 (Quay di -> Hat rac -> Quay ve)");
  ESP_LOGI(TAG, "  Phim g hoac G : TEST CHI QUAY DI Ngan 3 (%d ms, canh vach 180 do)", s_rotate_time_180_fwd_ms);
  ESP_LOGI(TAG, "  Phim b hoac B : TEST CHI QUAY VE Home   (%d ms, canh vach 0 do Home)", s_rotate_time_180_rev_ms);
  ESP_LOGI(TAG, "  Phim 7        : GIAM thoi gian QUAY DI Ngan 3 (-5 ms, Hien tai: %d ms)", s_rotate_time_180_fwd_ms);
  ESP_LOGI(TAG, "  Phim 8        : TANG thoi gian QUAY DI Ngan 3 (+5 ms, Hien tai: %d ms)", s_rotate_time_180_fwd_ms);
  ESP_LOGI(TAG, "  Phim 9        : GIAM thoi gian QUAY VE Ngan 3 (-5 ms, Hien tai: %d ms)", s_rotate_time_180_rev_ms);
  ESP_LOGI(TAG, "  Phim 0        : TANG thoi gian QUAY VE Ngan 3 (+5 ms, Hien tai: %d ms)", s_rotate_time_180_rev_ms);
  ESP_LOGI(TAG,
           "-----------------------------------------------------------------");
  ESP_LOGI(TAG, "CAC NGAN 90 DO (NGAN 2 VA NGAN 4) (±5ms, Tu dong luu Flash NVS):");
  ESP_LOGI(TAG, "  Phim 1, 2, 4  : Chay chu trinh Ngan 1 (Home), Ngan 2 (90 do), Ngan 4 (-90 do)");
  ESP_LOGI(TAG, "  Phim + hoac = : TANG thoi gian QUAY DI 90 do (+5 ms, Hien tai: %d ms)", s_rotate_time_90_ms);
  ESP_LOGI(TAG, "  Phim - hoac _ : GIAM thoi gian QUAY DI 90 do (-5 ms, Hien tai: %d ms)", s_rotate_time_90_ms);
  ESP_LOGI(TAG, "  Phim 6        : TANG thoi gian QUAY VE 90 do (+5 ms, Hien tai: %d ms)", s_rotate_time_90_rev_ms);
  ESP_LOGI(TAG, "  Phim 5        : GIAM thoi gian QUAY VE 90 do (-5 ms, Hien tai: %d ms)", s_rotate_time_90_rev_ms);
  ESP_LOGI(TAG, "  Phim y hoac Y : TEST CHI QUAY DI Ngan 2 (+90 do, %d ms)", s_rotate_time_90_ms);
  ESP_LOGI(TAG, "  Phim u hoac U : TEST CHI QUAY VE Ngan 2 (-90 do, %d ms)", s_rotate_time_90_rev_ms);
  ESP_LOGI(TAG, "  Phim h hoac H : TEST CHI QUAY DI Ngan 4 (-90 do, %d ms)", s_rotate_time_90_ms);
  ESP_LOGI(TAG, "  Phim j hoac J : TEST CHI QUAY VE Ngan 4 (+90 do, %d ms)", s_rotate_time_90_rev_ms);
  ESP_LOGI(TAG,
           "=================================================================");

  // Vòng lặp nhận lệnh trực tiếp từ phím bấm cổng UART Serial
  while (1) {
    int c = getchar();

    if (c == '\r' || c == '\n' || c == EOF) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (c >= '1' && c <= '4') {
      int selected = c - '0';
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Kich hoat chu trinh dong bo Ngan %d", selected);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_BIN_ACTION;
        cmd.bin_number = selected;
        strncpy(cmd.trash_label, "manual", sizeof(cmd.trash_label) - 1);
        snprintf(cmd.trash_display_name, sizeof(cmd.trash_display_name),
                 "Ngan %d", selected);
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'f' || c == 'F') {
      // Quay thuận 90 độ -> Hất rác -> Quay về lại Home
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test chu trinh quay thuan 90 do (Ngan 2)");
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_BIN_ACTION;
        cmd.bin_number = 2;
        strncpy(cmd.trash_label, "test_fwd", sizeof(cmd.trash_label) - 1);
        strncpy(cmd.trash_display_name, "Quay thuan 90",
                sizeof(cmd.trash_display_name) - 1);
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'r' || c == 'R') {
      // Quay nghịch 90 độ -> Hất rác -> Quay về lại Home
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test chu trinh quay nghich 90 do (Ngan 4)");
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_BIN_ACTION;
        cmd.bin_number = 4;
        strncpy(cmd.trash_label, "test_rev", sizeof(cmd.trash_label) - 1);
        strncpy(cmd.trash_display_name, "Quay nghich 90",
                sizeof(cmd.trash_display_name) - 1);
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'g' || c == 'G') {
      // Test CHỈ QUAY ĐI Ngăn 3 (180 độ) để canh vạch
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test CHI QUAY DI Ngan 3 (180 do): %d ms", s_rotate_time_180_fwd_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_3_FWD;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'b' || c == 'B') {
      // Test CHỈ QUAY VỀ Home từ Ngăn 3 để canh vạch
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test CHI QUAY VE Home tu Ngan 3: %d ms", s_rotate_time_180_rev_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_3_REV;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'y' || c == 'Y') {
      // Test CHỈ QUAY ĐI Ngăn 2 (+90 độ)
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test CHI QUAY DI Ngan 2 (+90 do): %d ms", s_rotate_time_90_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_2_FWD;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'u' || c == 'U') {
      // Test CHỈ QUAY VỀ Home từ Ngăn 2 (-90 độ)
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test CHI QUAY VE Home tu Ngan 2 (-90 do): %d ms", s_rotate_time_90_rev_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_2_REV;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'h' || c == 'H') {
      // Test CHỈ QUAY ĐI Ngăn 4 (-90 độ)
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test CHI QUAY DI Ngan 4 (-90 do): %d ms", s_rotate_time_90_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_4_FWD;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'j' || c == 'J') {
      // Test CHỈ QUAY VỀ Home từ Ngăn 4 (+90 độ)
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test CHI QUAY VE Home tu Ngan 4 (+90 do): %d ms", s_rotate_time_90_rev_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_4_REV;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 't' || c == 'T') {
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG,
                 "[SERIAL] Test chu trinh hat rac (Mo %d ms -> Doi %d ms -> Dong %d ms)",
                 s_dump_fwd_time_ms, s_dump_wait_ms, s_dump_rev_time_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_DUMP_ONLY;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'o' || c == 'O') {
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test quay MO hat rac (%d ms)", s_dump_fwd_time_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_SERVO_RAW_180;
        cmd.bin_number = 1;
        cmd.param = s_dump_fwd_time_ms;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'c' || c == 'C') {
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        ESP_LOGI(TAG, "[SERIAL] Test quay DONG ve vi tri cu (%d ms)", s_dump_rev_time_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_SERVO_RAW_180;
        cmd.bin_number = -1;
        cmd.param = s_dump_rev_time_ms;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == 'i' || c == 'I') {
      if (s_is_busy) {
        ESP_LOGW(TAG, "He thong dang ban thuc hien chu trinh, vui long doi...");
      } else {
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_SERVO_INVERT;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
      }
    } else if (c == '[' || c == '{') {
      s_dump_fwd_time_ms -= 5;
      if (s_dump_fwd_time_ms < 50)
        s_dump_fwd_time_ms = 50;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms HAT RAC - QUAY MO] GIAM thoi gian: %d ms (Da luu Flash NVS)",
               s_dump_fwd_time_ms);
    } else if (c == ']' || c == '}') {
      s_dump_fwd_time_ms += 5;
      if (s_dump_fwd_time_ms > 5000)
        s_dump_fwd_time_ms = 5000;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms HAT RAC - QUAY MO] TANG thoi gian: %d ms (Da luu Flash NVS)",
               s_dump_fwd_time_ms);
    } else if (c == '.' || c == '>') {
      int step = (c == '>') ? 5 : 1;
      s_dump_rest_angle += step;
      if (s_dump_rest_angle > 180) s_dump_rest_angle = 180;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB GOC DONG NGHÍ] TANG GOC DONG (+%d do): Hien tai %d do (Pulse: %ld us) (Da luu NVS)",
               step, s_dump_rest_angle, (long)angle_to_pulse_us(s_dump_rest_angle));
      servo_180_set_angle(s_dump_rest_angle);
      vTaskDelay(pdMS_TO_TICKS(250));
      servo_180_stop();
    } else if (c == ',' || c == '<') {
      int step = (c == '<') ? 5 : 1;
      s_dump_rest_angle -= step;
      if (s_dump_rest_angle < 0) s_dump_rest_angle = 0;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB GOC DONG NGHÍ] GIAM GOC DONG (-%d do): Hien tai %d do (Pulse: %ld us) (Da luu NVS)",
               step, s_dump_rest_angle, (long)angle_to_pulse_us(s_dump_rest_angle));
      servo_180_set_angle(s_dump_rest_angle);
      vTaskDelay(pdMS_TO_TICKS(250));
      servo_180_stop();
    } else if (c == ';' || c == ':') {
      s_dump_rev_time_ms -= 5;
      if (s_dump_rev_time_ms < 50)
        s_dump_rev_time_ms = 50;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms HAT RAC - TOC DO DONG] GIAM thoi gian: %d ms (Da luu Flash NVS)",
               s_dump_rev_time_ms);
    } else if (c == '\'' || c == '\"') {
      s_dump_rev_time_ms += 5;
      if (s_dump_rev_time_ms > 5000)
        s_dump_rev_time_ms = 5000;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms HAT RAC - TOC DO DONG] TANG thoi gian: %d ms (Da luu Flash NVS)",
               s_dump_rev_time_ms);
    } else if (c == 'x' || c == 'X') {
      ESP_LOGI(TAG, "[SERIAL] Dung / ngat xung PWM Servo hat rac (tha long)");
      servo_180_stop();
    } else if (c == '+' || c == '=') {
      s_rotate_time_90_ms += 5;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms 90 DO QUAY DI] TANG thoi gian quay DI: %d ms (Da luu Flash NVS)",
               s_rotate_time_90_ms);
    } else if (c == '-' || c == '_') {
      s_rotate_time_90_ms -= 5;
      if (s_rotate_time_90_ms < 100)
        s_rotate_time_90_ms = 100;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms 90 DO QUAY DI] GIAM thoi gian quay DI: %d ms (Da luu Flash NVS)",
               s_rotate_time_90_ms);
    } else if (c == '5') {
      s_rotate_time_90_rev_ms -= 5;
      if (s_rotate_time_90_rev_ms < 100)
        s_rotate_time_90_rev_ms = 100;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms 90 DO QUAY VE] GIAM thoi gian quay VE: %d ms (Da luu Flash NVS)",
               s_rotate_time_90_rev_ms);
    } else if (c == '6') {
      s_rotate_time_90_rev_ms += 5;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms 90 DO QUAY VE] TANG thoi gian quay VE: %d ms (Da luu Flash NVS)",
               s_rotate_time_90_rev_ms);
    } else if (c == '7') {
      s_rotate_time_180_fwd_ms -= 5;
      if (s_rotate_time_180_fwd_ms < 100)
        s_rotate_time_180_fwd_ms = 100;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms NGAN 3 DI] GIAM thoi gian DI: %d ms (Da luu Flash NVS)",
               s_rotate_time_180_fwd_ms);
    } else if (c == '8') {
      s_rotate_time_180_fwd_ms += 5;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms NGAN 3 DI] TANG thoi gian DI: %d ms (Da luu Flash NVS)",
               s_rotate_time_180_fwd_ms);
    } else if (c == '9') {
      s_rotate_time_180_rev_ms -= 5;
      if (s_rotate_time_180_rev_ms < 100)
        s_rotate_time_180_rev_ms = 100;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms NGAN 3 VE] GIAM thoi gian VE: %d ms (Da luu Flash NVS)",
               s_rotate_time_180_rev_ms);
    } else if (c == '0') {
      s_rotate_time_180_rev_ms += 5;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB ±5ms NGAN 3 VE] TANG thoi gian VE: %d ms (Da luu Flash NVS)",
               s_rotate_time_180_rev_ms);
    } else if (c == 's' || c == 'S') {
      ESP_LOGW(TAG, "[SERIAL] DUNG KHAN CAP SERVO 360");
      servo_360_stop();
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

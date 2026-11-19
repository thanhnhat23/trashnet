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
#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "lwip/sockets.h"

#include "wifi_config.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_is_wifi_connected = false;

#define SERVO_180_GPIO 25  // Servo 180 (MG995) hất rác
#define SERVO_360_GPIO 26  // Servo 360 (MG996R) quay mâm 4 góc
#define SERVO_LID_GPIO 27  // Servo 180 mở nắp thùng (Thùng 1)

// Cảm biến siêu âm #1: Đo mức rác đầy bên trong Thùng 1
#define TRIG_FULL_GPIO 5
#define ECHO_FULL_GPIO 18
#define DEFAULT_BIN1_FULL_THRESHOLD_CM 2.0f

// Cảm biến siêu âm #2: Cảm ứng mở nắp Thùng 1 (khi rác/tay gần <= 2cm)
#define TRIG_LID_GPIO 21
#define ECHO_LID_GPIO 22
#define DEFAULT_LID_TRIGGER_THRESHOLD_CM 2.0f

#define SERVO_180_REST_ANGLE 0

static int s_dump_rest_angle = 4;
static int s_dump_target_angle = 87;
static bool s_dump_inverted = false;
static int s_dump_fwd_time_ms = 500;
static int s_dump_rev_time_ms = 500;
static int s_dump_wait_ms = 1500;

// Servo 180 mở nắp thùng
static bool s_lid_inverted = true;     // Đảo chiều nắp: true = Chiều đảo (Đóng 10 độ -> Mở 0 độ)
static int s_lid_rest_angle = 10;      // Góc đóng nắp: 10 độ
static int s_lid_open_angle = 0;       // Góc mở nắp: 0 độ
static int s_current_lid_angle = 10;

// Trạng thái 2 cảm biến siêu âm
static float s_dist_full_cm = -1.0f;
static bool s_bin1_is_full = false;
static float s_bin1_full_threshold_cm = DEFAULT_BIN1_FULL_THRESHOLD_CM;

static float s_dist_lid_cm = -1.0f;
static bool s_lid_detected = false;
static float s_lid_trigger_threshold_cm = DEFAULT_LID_TRIGGER_THRESHOLD_CM;
static bool s_lid_auto_trigger_enabled = true;

#define SERVO_360_SPEED_FWD_US 1750
#define SERVO_360_SPEED_REV_US 1250
#define SERVO_360_STOP_US 1500

static int s_rotate_time_90_ms = 950;
static int s_rotate_time_90_rev_ms = 1010;
static int s_rotate_time_180_fwd_ms = 1850;
static int s_rotate_time_180_rev_ms = 1955;

static const char *TAG = "TRASHNET_ACTUATOR";
static int current_position = 0;
static int s_current_servo_angle = SERVO_180_REST_ANGLE;
static bool s_is_busy = false;
static int64_t s_busy_start_time = 0;
#define BUSY_WATCHDOG_TIMEOUT_US (25 * 1000000LL)
static char s_last_trash_name[32] = "San sang";

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
    nvs_set_i32(handle, "lidrest", s_lid_rest_angle);
    nvs_set_i32(handle, "lidopen", s_lid_open_angle);
    nvs_set_i32(handle, "fullth", (int32_t)(s_bin1_full_threshold_cm * 10.0f));
    nvs_set_i32(handle, "lidth", (int32_t)(s_lid_trigger_threshold_cm * 10.0f));
    nvs_set_u8(handle, "inv", s_dump_inverted ? 1 : 0);
    nvs_set_u8(handle, "lidinv", s_lid_inverted ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Da luu thong so vao Flash NVS");
  } else {
    ESP_LOGE(TAG, "Loi mo NVS: %s", esp_err_to_name(err));
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
    if (nvs_get_u8(handle, "lidinv", &u8val) == ESP_OK) {
      s_lid_inverted = (u8val != 0);
    } else {
      s_lid_inverted = true;
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
    if (nvs_get_i32(handle, "lidrest", &val) == ESP_OK) {
      s_lid_rest_angle = val;
    } else {
      s_lid_rest_angle = s_lid_inverted ? 10 : 0;
    }
    if (nvs_get_i32(handle, "lidopen", &val) == ESP_OK) {
      s_lid_open_angle = val;
    } else {
      s_lid_open_angle = s_lid_inverted ? 0 : 10;
    }
    if (nvs_get_i32(handle, "fullth", &val) == ESP_OK) {
      s_bin1_full_threshold_cm = val / 10.0f;
    } else {
      s_bin1_full_threshold_cm = 2.0f;
    }
    if (nvs_get_i32(handle, "lidth", &val) == ESP_OK) {
      s_lid_trigger_threshold_cm = val / 10.0f;
    } else {
      s_lid_trigger_threshold_cm = 2.0f;
    }
    nvs_close(handle);
  } else {
    s_dump_rest_angle = s_dump_inverted ? 180 : 0;
    s_dump_target_angle = 90;
    s_lid_inverted = true;
    s_lid_rest_angle = 10;
    s_lid_open_angle = 0;
    s_bin1_full_threshold_cm = 2.0f;
    s_lid_trigger_threshold_cm = 2.0f;
  }
}

typedef enum {
  CMD_BIN_ACTION = 0,
  CMD_DUMP_ONLY,
  CMD_SERVO_ANGLE,
  CMD_SERVO_INVERT,
  CMD_SERVO_RAW_180,
  CMD_ROTATE_3_FWD,
  CMD_ROTATE_3_REV,
  CMD_ROTATE_2_FWD,
  CMD_ROTATE_2_REV,
  CMD_ROTATE_4_FWD,
  CMD_ROTATE_4_REV,
  CMD_LID_OPEN,
  CMD_LID_CLOSE,
  CMD_LID_ANGLE,
  CMD_LID_INVERT,
  CMD_LID_TRIGGER_CYCLE
} cmd_type_t;

typedef struct {
  cmd_type_t type;
  int bin_number;
  int param;
  char trash_label[32];
  char trash_display_name[32];
} bin_command_t;

static QueueHandle_t s_bin_cmd_queue = NULL;
static SemaphoreHandle_t s_servo_mutex = NULL;

// Servo 180 & Servo 360 Driver
static void servo_hardware_init(void) {
  ledc_timer_config_t timer_config = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .duty_resolution = LEDC_TIMER_16_BIT,
                                      .timer_num = LEDC_TIMER_0,
                                      .freq_hz = 50,
                                      .clk_cfg = LEDC_AUTO_CLK};
  ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

  // Kênh 0: Servo 180 (GPIO 25 / D25)
  ledc_channel_config_t ch180_config = {.gpio_num = SERVO_180_GPIO,
                                        .speed_mode = LEDC_LOW_SPEED_MODE,
                                        .channel = LEDC_CHANNEL_0,
                                        .timer_sel = LEDC_TIMER_0,
                                        .duty = 0,
                                        .hpoint = 0};
  ESP_ERROR_CHECK(ledc_channel_config(&ch180_config));

  // Kênh 1: Servo 360 (GPIO 26 / D26)
  ledc_channel_config_t ch360_config = {.gpio_num = SERVO_360_GPIO,
                                        .speed_mode = LEDC_LOW_SPEED_MODE,
                                        .channel = LEDC_CHANNEL_1,
                                        .timer_sel = LEDC_TIMER_0,
                                        .duty = 0,
                                        .hpoint = 0};
  ESP_ERROR_CHECK(ledc_channel_config(&ch360_config));

  // Kênh 2: Servo 180 mở nắp thùng (GPIO 27 / D27)
  ledc_channel_config_t ch_lid_config = {.gpio_num = SERVO_LID_GPIO,
                                         .speed_mode = LEDC_LOW_SPEED_MODE,
                                         .channel = LEDC_CHANNEL_2,
                                         .timer_sel = LEDC_TIMER_0,
                                         .duty = 0,
                                         .hpoint = 0};
  ESP_ERROR_CHECK(ledc_channel_config(&ch_lid_config));
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

// ====================================================================
// DRIVER SERVO 180 MỞ NẮP THÙNG (GPIO 27)
// ====================================================================
static void servo_lid_set_pulse_direct(uint32_t pulse_us) {
  uint32_t duty = (pulse_us * 65535) / 20000;
  if (s_servo_mutex) xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
  if (s_servo_mutex) xSemaphoreGive(s_servo_mutex);
}

static void servo_lid_stop(void) {
  if (s_servo_mutex) xSemaphoreTake(s_servo_mutex, portMAX_DELAY);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
  if (s_servo_mutex) xSemaphoreGive(s_servo_mutex);
}

static void servo_lid_set_angle(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  s_current_lid_angle = angle;
  servo_lid_set_pulse_direct(angle_to_pulse_us(angle));
  ESP_LOGI(TAG, "[SERVO LID - G%d] Dat goc %d do", SERVO_LID_GPIO, angle);
}

static void servo_lid_open(void) {
  ESP_LOGI(TAG, ">>> [SERVO LID] MO NAP THUNG: %d do", s_lid_open_angle);
  servo_lid_set_angle(s_lid_open_angle);
}

static void servo_lid_close(void) {
  ESP_LOGI(TAG, ">>> [SERVO LID] DONG NAP THUNG: %d do", s_lid_rest_angle);
  servo_lid_set_angle(s_lid_rest_angle);
  vTaskDelay(pdMS_TO_TICKS(250));
  servo_lid_stop();
}

static void servo_lid_toggle_direction(void) {
  s_lid_inverted = !s_lid_inverted;
  if (s_lid_inverted) {
    s_lid_rest_angle = 10;
    s_lid_open_angle = 0;
    ESP_LOGW(TAG, ">>> [SERVO LID] DA DAO SANG CHIEU NGUOC! (Dong: 10 do -> Mo: 0 do)");
  } else {
    s_lid_rest_angle = 0;
    s_lid_open_angle = 10;
    ESP_LOGW(TAG, ">>> [SERVO LID] DA CHUYEN VE CHIEU THUAN! (Dong: 0 do -> Mo: 10 do)");
  }
  servo_lid_set_angle(s_lid_rest_angle);
  vTaskDelay(pdMS_TO_TICKS(250));
  servo_lid_stop();
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

// ====================================================================
// DRIVER CẢM BIẾN SIÊU ÂM HC-SR04 (2 CẢM BIẾN RIÊNG BIỆT)
// ====================================================================
static void ultrasonic_hardware_init(void) {
  gpio_config_t trig_conf = {
      .pin_bit_mask = (1ULL << TRIG_FULL_GPIO) | (1ULL << TRIG_LID_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&trig_conf));
  gpio_set_level(TRIG_FULL_GPIO, 0);
  gpio_set_level(TRIG_LID_GPIO, 0);

  gpio_config_t echo_conf = {
      .pin_bit_mask = (1ULL << ECHO_FULL_GPIO) | (1ULL << ECHO_LID_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&echo_conf));
  ESP_LOGI(TAG, "Ultrasonic Initialized: Full [TRIG G%d, ECHO G%d], Lid [TRIG G%d, ECHO G%d]",
           TRIG_FULL_GPIO, ECHO_FULL_GPIO, TRIG_LID_GPIO, ECHO_LID_GPIO);
}

static float ultrasonic_measure_cm(gpio_num_t trig_pin, gpio_num_t echo_pin) {
  if (gpio_get_level(echo_pin) != 0) {
    int64_t t_wait_low = esp_timer_get_time();
    while (gpio_get_level(echo_pin) != 0) {
      if (esp_timer_get_time() - t_wait_low > 2000) return -1.0f;
    }
  }

  gpio_set_level(trig_pin, 0);
  esp_rom_delay_us(4);
  gpio_set_level(trig_pin, 1);
  esp_rom_delay_us(15);
  gpio_set_level(trig_pin, 0);

  int64_t t_wait_high = esp_timer_get_time();
  while (gpio_get_level(echo_pin) == 0) {
    if (esp_timer_get_time() - t_wait_high > 12000) return -2.0f;
  }

  int64_t t_echo_start = esp_timer_get_time();
  while (gpio_get_level(echo_pin) == 1) {
    if (esp_timer_get_time() - t_echo_start > 30000) return -3.0f;
  }
  int64_t t_echo_end = esp_timer_get_time();

  int64_t duration_us = t_echo_end - t_echo_start;
  float dist_cm = (float)duration_us / 58.2f;

  if (dist_cm < 0.5f || dist_cm > 400.0f) return -4.0f;
  return dist_cm;
}

static void ultrasonic_task(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  int full_detect_count = 0;
  int full_clear_count = 0;
  int lid_detect_count = 0;
  int lid_cooldown = 0;

  while (1) {
    // 1. Đo cảm biến 1: Rác đầy trong Thùng 1
    float d1 = ultrasonic_measure_cm(TRIG_FULL_GPIO, ECHO_FULL_GPIO);
    if (d1 >= 0.5f && d1 <= 400.0f) {
      s_dist_full_cm = d1;
      if (d1 <= s_bin1_full_threshold_cm) {
        full_detect_count++;
        full_clear_count = 0;
        if (full_detect_count >= 2) {
          s_bin1_is_full = true;
        }
      } else {
        full_clear_count++;
        if (full_clear_count >= 2) {
          s_bin1_is_full = false;
          full_detect_count = 0;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. Đo cảm biến 2: Cảm ứng mở nắp Thùng 1
    float d2 = ultrasonic_measure_cm(TRIG_LID_GPIO, ECHO_LID_GPIO);
    if (d2 >= 0.5f && d2 <= 400.0f) {
      s_dist_lid_cm = d2;
      if (d2 <= s_lid_trigger_threshold_cm) {
        lid_detect_count++;
        s_lid_detected = true;
        if (s_lid_auto_trigger_enabled && lid_detect_count >= 2 && !s_is_busy && lid_cooldown <= 0) {
          ESP_LOGI(TAG, ">>> [SIÊU ÂM CẢM ỨNG NẮP] Phát hiện rác gần %.1f cm <= %.1f cm -> KÍCH HOẠT MỞ NẮP VÀ HẤT RÁC!",
                   d2, s_lid_trigger_threshold_cm);
          bin_command_t cmd;
          memset(&cmd, 0, sizeof(cmd));
          cmd.type = CMD_LID_TRIGGER_CYCLE;
          xQueueSend(s_bin_cmd_queue, &cmd, 0);
          lid_cooldown = 50; // Cooldown 5 giây tránh kích hoạt lặp
        }
      } else {
        lid_detect_count = 0;
        s_lid_detected = false;
      }
    }

    if (lid_cooldown > 0) lid_cooldown--;
    vTaskDelay(pdMS_TO_TICKS(80));
  }
}

static void hardware_init(void) {
  if (s_servo_mutex == NULL) {
    s_servo_mutex = xSemaphoreCreateMutex();
  }
  servo_hardware_init();
  servo_360_stop();
  servo_180_stop();
  servo_lid_close();

  ultrasonic_hardware_init();

  ESP_LOGI(TAG, "Hardware Initialized (Servo 360 [G%d] + Servo 180 [G%d] + Servo Lid [G%d])",
           SERVO_360_GPIO, SERVO_180_GPIO, SERVO_LID_GPIO);
}

// ====================================================================
// MA TRẬN THỜI GIAN QUAY 4x4 GIỮA CÁC THÙNG RÁC (CHUẨN THỰC TẾ)
// ====================================================================
typedef struct {
  int dir;          // 0: dung yen, 1: thuan, -1: nghich
  int duration_ms;  // thoi gian quay ms
} rotation_step_t;

static rotation_step_t get_rotation_step(int from_bin, int to_bin) {
  rotation_step_t step = {0, 0};
  if (from_bin < 1 || from_bin > 4 || to_bin < 1 || to_bin > 4 || from_bin == to_bin) {
    return step;
  }

  // Ma tran chuyen trang thai 4x4 chuan xac giua cac ngan:
  if (from_bin == 1) {
    if (to_bin == 2) { step.dir = 1; step.duration_ms = s_rotate_time_90_ms; }           // Thuan 90: 950ms
    else if (to_bin == 3) { step.dir = 1; step.duration_ms = s_rotate_time_180_fwd_ms; } // Thuan 180: 1850ms
    else if (to_bin == 4) { step.dir = -1; step.duration_ms = s_rotate_time_90_rev_ms; } // Nghich 90: 1010ms
  } else if (from_bin == 2) {
    if (to_bin == 1) { step.dir = -1; step.duration_ms = s_rotate_time_90_rev_ms; }      // Nghich 90: 1010ms
    else if (to_bin == 3) { step.dir = 1; step.duration_ms = s_rotate_time_90_ms; }     // Thuan 90: 950ms
    else if (to_bin == 4) { step.dir = 1; step.duration_ms = s_rotate_time_180_fwd_ms; } // Thuan 180: 1850ms
  } else if (from_bin == 3) {
    if (to_bin == 1) { step.dir = -1; step.duration_ms = s_rotate_time_180_rev_ms; }     // Nghich 180: 1955ms
    else if (to_bin == 2) { step.dir = -1; step.duration_ms = s_rotate_time_90_rev_ms; } // Nghich 90: 1010ms
    else if (to_bin == 4) { step.dir = 1; step.duration_ms = s_rotate_time_90_ms; }     // Thuan 90: 950ms
  } else if (from_bin == 4) {
    if (to_bin == 1) { step.dir = 1; step.duration_ms = s_rotate_time_90_ms; }          // Thuan 90: 950ms
    else if (to_bin == 2) { step.dir = -1; step.duration_ms = s_rotate_time_180_rev_ms; } // Nghich 180: 1955ms
    else if (to_bin == 3) { step.dir = -1; step.duration_ms = s_rotate_time_90_rev_ms; } // Nghich 90: 1010ms
  }
  return step;
}

static void process_bin_action(int bin_number, const char *display_name) {
  const char *trash_categories[4] = {
      "GIAY",
      "THUY TINH",
      "RAC KHAC",
      "NHUA (PET)"
  };

  if (bin_number < 1 || bin_number > 4) return;
  s_is_busy = true;
  s_busy_start_time = esp_timer_get_time();

  int target_idx = bin_number - 1;
  const char *category = trash_categories[target_idx];

  char line2[32];
  if (display_name && strlen(display_name) > 0) {
    snprintf(line2, sizeof(line2), "%-16.16s", display_name);
  } else {
    snprintf(line2, sizeof(line2), "Rac %-12.12s", category);
  }

  strncpy(s_last_trash_name, line2, sizeof(s_last_trash_name) - 1);
  s_last_trash_name[sizeof(s_last_trash_name) - 1] = '\0';

  ESP_LOGI(TAG, ">>> CHU TRINH: Ngan %d [%s] - %s", bin_number, category, line2);

  int from_bin = current_position + 1;
  rotation_step_t step_go = get_rotation_step(from_bin, bin_number);

  // BƯỚC 1: Xoay mâm (đối với ngăn 2, 3, 4)
  if (step_go.duration_ms > 0) {
    current_position = target_idx;
    ESP_LOGI(TAG, ">>> [BƯỚC 1: XOAY MÂM] Tu Ngan %d -> Ngan %d: Chieu %d, Thoi gian %d ms",
             from_bin, bin_number, step_go.dir, step_go.duration_ms);
    servo_360_rotate_raw(step_go.dir, step_go.duration_ms);
    vTaskDelay(pdMS_TO_TICKS(400));
  } else {
    current_position = target_idx;
  }

  // BƯỚC 2: Mở nắp thùng (áp dụng cho tất cả: Thùng 1 mở tại chỗ, Thùng 2/3/4 mở sau khi xoay)
  ESP_LOGI(TAG, ">>> [BƯỚC 2: MỞ NẮP THÙNG] Mo nap %d do", s_lid_open_angle);
  servo_lid_open();
  vTaskDelay(pdMS_TO_TICKS(400));

  // BƯỚC 3: Hất rác xuống thùng (Servo 180 hất rác)
  ESP_LOGI(TAG, ">>> [BƯỚC 3: HẤT RÁC] Hat rac xuong ngan %d", bin_number);
  servo_180_dump_trash();
  vTaskDelay(pdMS_TO_TICKS(300));

  // BƯỚC 4: Đóng nắp thùng (Servo 180 nắp: về lại s_lid_rest_angle)
  ESP_LOGI(TAG, ">>> [BƯỚC 4: ĐÓNG NẮP THÙNG] Dong nap ve %d do", s_lid_rest_angle);
  servo_lid_close();
  vTaskDelay(pdMS_TO_TICKS(300));

  // BƯỚC 5: Xoay mâm về lại Home (Ngăn 1) nếu vừa đổ ở Ngăn 2, 3, 4
  if (bin_number != 1) {
    rotation_step_t step_ret = get_rotation_step(bin_number, 1);
    if (step_ret.duration_ms > 0) {
      ESP_LOGI(TAG, ">>> [BƯỚC 5: VỀ HOME] Tu Ngan %d -> Ngan 1: Chieu %d, Thoi gian %d ms",
               bin_number, step_ret.dir, step_ret.duration_ms);
      servo_360_rotate_raw(step_ret.dir, step_ret.duration_ms);
      vTaskDelay(pdMS_TO_TICKS(400));
    }
  }
  current_position = 0;

  s_is_busy = false;
  ESP_LOGI(TAG, ">>> HOAN TAT NGAN %d (Da ve Home Ngan 1)", bin_number);
}

static void actuator_worker_task(void *pvParameters) {
  bin_command_t cmd;
  while (1) {
    if (s_is_busy && s_busy_start_time > 0) {
      int64_t elapsed = esp_timer_get_time() - s_busy_start_time;
      if (elapsed > BUSY_WATCHDOG_TIMEOUT_US) {
        ESP_LOGW(TAG, ">>> [WATCHDOG] Force reset s_is_busy.");
        s_is_busy = false;
        s_busy_start_time = 0;
      }
    }

    if (xQueueReceive(s_bin_cmd_queue, &cmd, pdMS_TO_TICKS(500)) == pdTRUE) {
      s_is_busy = true;
      s_busy_start_time = esp_timer_get_time();
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
          ESP_LOGI(TAG, ">>> [WORKER] Test CHI QUAY DI Ngan 4 (-90 do) (%d ms)", s_rotate_time_90_rev_ms);
          servo_360_rotate_raw(-1, s_rotate_time_90_rev_ms);
          break;
        case CMD_ROTATE_4_REV:
          ESP_LOGI(TAG, ">>> [WORKER] Test CHI QUAY VE Home tu Ngan 4 (+90 do) (%d ms)", s_rotate_time_90_ms);
          servo_360_rotate_raw(1, s_rotate_time_90_ms);
          break;
        case CMD_LID_OPEN:
          ESP_LOGI(TAG, ">>> [WORKER] Lenh MO NAP: %d do", s_lid_open_angle);
          servo_lid_open();
          break;
        case CMD_LID_CLOSE:
          ESP_LOGI(TAG, ">>> [WORKER] Lenh DONG NAP: %d do", s_lid_rest_angle);
          servo_lid_close();
          break;
        case CMD_LID_ANGLE:
          ESP_LOGI(TAG, ">>> [WORKER] Dat goc Servo NAP: %d do", cmd.param);
          servo_lid_set_angle(cmd.param);
          break;
        case CMD_LID_INVERT:
          ESP_LOGI(TAG, ">>> [WORKER] Dao chieu quay Servo NAP");
          servo_lid_toggle_direction();
          calib_save_to_nvs();
          break;
        case CMD_LID_TRIGGER_CYCLE:
          ESP_LOGI(TAG, ">>> [WORKER] Cam bien HC-SR04 nap kich hoat chu trinh Ngan 1");
          process_bin_action(1, "Tu dong SR04");
          break;
        default:
          ESP_LOGW(TAG, ">>> [WORKER] Lenh khong xac dinh (cmd.type=%d)", cmd.type);
          break;
      }
      s_is_busy = false;
      s_busy_start_time = 0;
    }
  }
}

// HTTP Server điều khiển thiết bị

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

  // Đẩy vào hàng đợi tuần tự (chờ tối đa 150ms nếu hàng đợi đang bận xử lý)
  BaseType_t q_res = xQueueSend(s_bin_cmd_queue, &cmd, pdMS_TO_TICKS(150));
  if (q_res != pdTRUE) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"status\":\"busy\",\"message\":\"He thong dang ban xu ly chu trinh truoc\"}", HTTPD_RESP_USE_STRLEN);
  }

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

  char resp[768];
  snprintf(resp, sizeof(resp),
           "{\"online\":true,\"current_bin\":%d,\"current_angle\":%d,\"servo_angle\":%d,"
           "\"rotate_time_ms\":%d,\"rotate_90_ms\":%d,\"rotate_90_rev_ms\":%d,\"rotate_180_fwd_ms\":%d,\"rotate_180_rev_ms\":%d,"
           "\"dump_fwd_ms\":%d,\"dump_rev_ms\":%d,\"dump_wait_ms\":%d,\"inverted\":%s,"
           "\"rest_angle\":%d,\"dump_angle\":%d,\"lid_angle\":%d,\"lid_rest_angle\":%d,\"lid_open_angle\":%d,\"lid_inverted\":%s,"
           "\"bin1_full\":%s,\"dist_full_cm\":%.1f,\"threshold_full_cm\":%.1f,"
           "\"lid_detected\":%s,\"dist_lid_cm\":%.1f,\"threshold_lid_cm\":%.1f,\"lid_auto_trigger\":%s,"
           "\"is_busy\":%s,\"last_trash\":\"%s\","
           "\"distance_cm\":%.1f,\"object_detected\":%s,\"threshold_cm\":%.1f}",
           current_position + 1, current_position * 90, s_current_servo_angle,
           s_rotate_time_90_ms, s_rotate_time_90_ms, s_rotate_time_90_rev_ms, s_rotate_time_180_fwd_ms, s_rotate_time_180_rev_ms,
           s_dump_fwd_time_ms, s_dump_rev_time_ms, s_dump_wait_ms,
           s_dump_inverted ? "true" : "false", s_dump_rest_angle,
           s_dump_target_angle, s_current_lid_angle, s_lid_rest_angle, s_lid_open_angle,
           s_lid_inverted ? "true" : "false",
           s_bin1_is_full ? "true" : "false", s_dist_full_cm, s_bin1_full_threshold_cm,
           s_lid_detected ? "true" : "false", s_dist_lid_cm, s_lid_trigger_threshold_cm,
           s_lid_auto_trigger_enabled ? "true" : "false",
           s_is_busy ? "true" : "false", s_last_trash_name,
           s_dist_full_cm, s_bin1_is_full ? "true" : "false", s_bin1_full_threshold_cm);
  return httpd_resp_send(req, resp, strlen(resp));
}

// GET /distance: Lấy thông số khoảng cách cảm biến siêu âm riêng biệt (< 1ms)
static esp_err_t http_distance_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Connection", "close");
  char resp[256];
  snprintf(resp, sizeof(resp),
           "{\"bin1_full\":%s,\"dist_full_cm\":%.1f,\"threshold_full_cm\":%.1f,"
           "\"lid_detected\":%s,\"dist_lid_cm\":%.1f,\"threshold_lid_cm\":%.1f,"
           "\"lid_auto_trigger\":%s,\"is_busy\":%s}",
           s_bin1_is_full ? "true" : "false", s_dist_full_cm, s_bin1_full_threshold_cm,
           s_lid_detected ? "true" : "false", s_dist_lid_cm, s_lid_trigger_threshold_cm,
           s_lid_auto_trigger_enabled ? "true" : "false", s_is_busy ? "true" : "false");
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
        httpd_query_key_value(query, "rest_ang", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "dump_rest_angle", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 0 && v <= 180) s_dump_rest_angle = v;
    }
    if (httpd_query_key_value(query, "dumptgt", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "dump_ang", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "target_ang", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "dump_target_angle", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 0 && v <= 180) s_dump_target_angle = v;
    }
    if (httpd_query_key_value(query, "lid_inv", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "lidinv", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      s_lid_inverted = (v != 0);
      s_lid_rest_angle = s_lid_inverted ? 10 : 0;
      s_lid_open_angle = s_lid_inverted ? 0 : 10;
    }
    if (httpd_query_key_value(query, "lidrest", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "lid_rest", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 0 && v <= 180) s_lid_rest_angle = v;
    }
    if (httpd_query_key_value(query, "lidopen", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "lid_open", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      if (v >= 0 && v <= 180) s_lid_open_angle = v;
    }
    if (httpd_query_key_value(query, "fullth", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "full_th", val, sizeof(val)) == ESP_OK) {
      float v = atof(val);
      if (v >= 0.5f && v <= 100.0f) s_bin1_full_threshold_cm = v;
    }
    if (httpd_query_key_value(query, "lidth", val, sizeof(val)) == ESP_OK ||
        httpd_query_key_value(query, "lid_th", val, sizeof(val)) == ESP_OK) {
      float v = atof(val);
      if (v >= 0.5f && v <= 100.0f) s_lid_trigger_threshold_cm = v;
    }
    if (httpd_query_key_value(query, "lid_auto", val, sizeof(val)) == ESP_OK) {
      int v = atoi(val);
      s_lid_auto_trigger_enabled = (v != 0);
    }
    calib_save_to_nvs();
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Connection", "close");
  char resp[580];
  snprintf(resp, sizeof(resp),
           "{\"status\":\"ok\",\"rotate_90_ms\":%d,\"rotate_90_rev_ms\":%d,\"rotate_180_fwd_ms\":%d,\"rotate_180_rev_ms\":%d,"
           "\"dump_fwd_ms\":%d,\"dump_rev_ms\":%d,\"dump_wait_ms\":%d,\"rest_angle\":%d,\"dump_angle\":%d,\"inverted\":%s,"
           "\"lid_rest\":%d,\"lid_open\":%d,\"lid_inverted\":%s,\"full_th\":%.1f,\"lid_th\":%.1f,\"lid_auto\":%s}",
           s_rotate_time_90_ms, s_rotate_time_90_rev_ms, s_rotate_time_180_fwd_ms, s_rotate_time_180_rev_ms,
           s_dump_fwd_time_ms, s_dump_rev_time_ms, s_dump_wait_ms,
           s_dump_rest_angle, s_dump_target_angle,
           s_dump_inverted ? "true" : "false",
           s_lid_rest_angle, s_lid_open_angle,
           s_lid_inverted ? "true" : "false",
           s_bin1_full_threshold_cm, s_lid_trigger_threshold_cm,
           s_lid_auto_trigger_enabled ? "true" : "false");
  return httpd_resp_send(req, resp, strlen(resp));
}

// GET /servo?dump=1 (hoặc ?invert=1 hoặc ?fwd=500 hoặc ?rev=500 hoặc ?time=500
// hoặc ?angle=90 hoặc ?lid=open / lid=close hoặc ?lid_angle=10 hoặc ?lid_invert=1): Test Servo BẤT ĐỒNG BỘ qua Queue
static esp_err_t http_servo_handler(httpd_req_t *req) {
  char query[128] = {0};

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char val[16];
    if (httpd_query_key_value(query, "dump", val, sizeof(val)) == ESP_OK) {
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_DUMP_ONLY;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (httpd_query_key_value(query, "lid", val, sizeof(val)) == ESP_OK) {
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      if (strcmp(val, "open") == 0 || strcmp(val, "1") == 0) {
        cmd.type = CMD_LID_OPEN;
      } else {
        cmd.type = CMD_LID_CLOSE;
      }
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (httpd_query_key_value(query, "lid_invert", val, sizeof(val)) == ESP_OK ||
               httpd_query_key_value(query, "lid_inv", val, sizeof(val)) == ESP_OK) {
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_LID_INVERT;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (httpd_query_key_value(query, "lid_angle", val, sizeof(val)) == ESP_OK) {
      int angle = atoi(val);
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_LID_ANGLE;
      cmd.param = angle;
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
    char from_val[16], to_val[16];
    if (httpd_query_key_value(query, "from", from_val, sizeof(from_val)) == ESP_OK &&
        httpd_query_key_value(query, "to", to_val, sizeof(to_val)) == ESP_OK) {
      int f = atoi(from_val);
      int t = atoi(to_val);
      if (f >= 1 && f <= 4 && t >= 1 && t <= 4) {
        rotation_step_t step = get_rotation_step(f, t);
        ESP_LOGI(TAG, "[API /rotate] Ma tran quay: Ngan %d -> Ngan %d (Dir=%d, Time=%d ms)",
                 f, t, step.dir, step.duration_ms);
        if (step.duration_ms > 0) {
          servo_360_rotate_raw(step.dir, step.duration_ms);
        }
        current_position = t - 1;
        handled = true;
      }
    } else if (httpd_query_key_value(query, "test", test_val, sizeof(test_val)) == ESP_OK) {
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
        ESP_LOGI(TAG, "[API /rotate] Queue Test CHI QUAY DI Ngan 4 (-90 do): %d ms", s_rotate_time_90_rev_ms);
        bin_command_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CMD_ROTATE_4_FWD;
        xQueueSend(s_bin_cmd_queue, &cmd, 0);
        handled = true;
      } else if (strcmp(test_val, "4_rev") == 0) {
        ESP_LOGI(TAG, "[API /rotate] Queue Test CHI QUAY VE Home tu Ngan 4 (+90 do): %d ms", s_rotate_time_90_ms);
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

    httpd_uri_t distance_uri = {.uri = "/distance",
                                .method = HTTP_GET,
                                .handler = http_distance_handler,
                                .user_ctx = NULL};
    httpd_register_uri_handler(server, &distance_uri);

    ESP_LOGI(TAG, "HTTP Server da khoi dong tren port 80!");
  }
  return server;
}

// Phát hiện IP thiết bị tự động qua UDP Broadcast
static void broadcast_device_ip(const char *dev_name, const esp_ip4_addr_t *ip) {
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
  ESP_LOGI(TAG, "[AUTO-IP] Da phat UDP Broadcast thong bao IP: %s", msg);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_is_wifi_connected = false;
    s_retry_num++;
    ESP_LOGW(TAG, "Mat tin hieu WiFi (beacon timeout). Dang thu ket noi lai lan %d...", s_retry_num);
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(
        TAG,
        "\n\n"
        "*****************************************************************\n"
        "**                                                             **\n"
        "**  >>> IP ESP32 ACTUATOR (SERVO 360 [G26] + SERVO 180 [G25] + LID [G27]): **\n"
        "**  >>> http://" IPSTR "                                       **\n"
        "**  >>> DAN DIA CHI NAY VAO WEB TRASHNET DE DIEU KHIEN         **\n"
        "**                                                             **\n"
        "*****************************************************************\n",
        IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    s_is_wifi_connected = true;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    broadcast_device_ip("ACTUATOR", &event->ip_info.ip);
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
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));  // Tat che do tiet kiem nang luong WiFi de chong rot mang (bcn_timeout)

  ESP_LOGI(TAG, "WiFi STA khoi tao hoan tat. Dang ket noi toi SSID: %s",
           CONFIG_ESP_WIFI_SSID);
}

void app_main(void) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  hardware_init();

  // Đọc thông số hiệu chuẩn đã lưu từ bộ nhớ Flash NVS
  calib_load_from_nvs();

  // 3. Khởi tạo hàng đợi lệnh và Task xử lý ngầm
  s_bin_cmd_queue = xQueueCreate(10, sizeof(bin_command_t));
  xTaskCreate(actuator_worker_task, "actuator_worker", 4096, NULL, 5, NULL);
  xTaskCreate(ultrasonic_task, "ultrasonic_task", 3072, NULL, 4, NULL);

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
  ESP_LOGI(TAG, "  🎯 CAN CHINH 2 DAU GOC SERVO HAT RAC (G19):");
  ESP_LOGI(TAG, "    1. GOC DONG NGHI (Dau 1 - Chong bi lo khi ve 0 do):");
  ESP_LOGI(TAG,
           "       Phim , hoac < : GIAM goc dong nghi (-1 do / -5 do, Hien tai: %d do, Tu dong luu NVS)",
           s_dump_rest_angle);
  ESP_LOGI(TAG,
           "       Phim . hoac > : TANG goc dong nghi (+1 do / +5 do, Hien tai: %d do, Tu dong luu NVS)",
           s_dump_rest_angle);
  ESP_LOGI(TAG, "    2. GOC MO HAT RAC (Dau 2 - Chinh goc do mo xuong 87 do, v.v.):");
  ESP_LOGI(TAG,
           "       Phim k/K (hoac n/N): GIAM goc mo hat rac (-1 do / -5 do, Hien tai: %d do, Tu dong luu NVS)",
           s_dump_target_angle);
  ESP_LOGI(TAG,
           "       Phim l/L (hoac m/M): TANG goc mo hat rac (+1 do / +5 do, Hien tai: %d do, Tu dong luu NVS)",
           s_dump_target_angle);
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
  ESP_LOGI(TAG, "  Phim h hoac H : TEST CHI QUAY DI Ngan 4 (-90 do, %d ms)", s_rotate_time_90_rev_ms);
  ESP_LOGI(TAG, "  Phim j hoac J : TEST CHI QUAY VE Ngan 4 (+90 do, %d ms)", s_rotate_time_90_ms);
  ESP_LOGI(TAG,
           "-----------------------------------------------------------------");
  ESP_LOGI(TAG, "📦 CAN CHINH SERVO MO NAP (G27) & CAM BIEN SIEU AM HC-SR04:");
  ESP_LOGI(TAG, "  Phim w hoac W : DAO CHIEU SERVO NAP (Hien tai: %s)", s_lid_inverted ? "CHIEU NGUOC [Dong: 10 do -> Mo: 0 do]" : "CHIEU THUAN [Dong: 0 do -> Mo: 10 do]");
  ESP_LOGI(TAG, "  Phim p hoac P : TEST MO NAP (%d do)", s_lid_open_angle);
  ESP_LOGI(TAG, "  Phim q hoac Q : TEST DONG NAP (%d do)", s_lid_rest_angle);
  ESP_LOGI(TAG, "  Phim v hoac V : TANG goc mo nap (+1 do / +5 do, Hien tai: %d do, Tu dong luu NVS)", s_lid_open_angle);
  ESP_LOGI(TAG, "  Phim z hoac Z : GIAM goc mo nap (-1 do / -5 do, Hien tai: %d do, Tu dong luu NVS)", s_lid_open_angle);
  ESP_LOGI(TAG, "  Phim a hoac A : BAT/TAT tu dong mo nap bang HC-SR04 (Hien tai: %s)", s_lid_auto_trigger_enabled ? "BAT" : "TAT");
  ESP_LOGI(TAG, "  Cam bien 1 (Bao day Thung 1 - TRIG G5, ECHO G18): Nguong %.1f cm (Hien tai: %.1f cm -> %s)",
           s_bin1_full_threshold_cm, s_dist_full_cm, s_bin1_is_full ? "DAY" : "CHUA DAY");
  ESP_LOGI(TAG, "  Cam bien 2 (Mo nap Thung 1   - TRIG G21, ECHO G22): Nguong %.1f cm (Hien tai: %.1f cm -> %s)",
           s_lid_trigger_threshold_cm, s_dist_lid_cm, s_lid_detected ? "PHAT HIEN" : "KHONG CO");
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
        ESP_LOGI(TAG, "[SERIAL] Test CHI QUAY DI Ngan 4 (-90 do): %d ms", s_rotate_time_90_rev_ms);
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
        ESP_LOGI(TAG, "[SERIAL] Test CHI QUAY VE Home tu Ngan 4 (+90 do): %d ms", s_rotate_time_90_ms);
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
    } else if (c == 'l' || c == 'L' || c == 'm' || c == 'M') {
      int step = (c == 'L' || c == 'M') ? 5 : 1;
      s_dump_target_angle += step;
      if (s_dump_target_angle > 180) s_dump_target_angle = 180;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB GOC HAT RAC / MO NAP] TANG GOC MO (+%d do): Hien tai %d do (Pulse: %ld us) (Da luu NVS)",
               step, s_dump_target_angle, (long)angle_to_pulse_us(s_dump_target_angle));
      servo_180_set_angle(s_dump_target_angle);
      vTaskDelay(pdMS_TO_TICKS(250));
      servo_180_stop();
    } else if (c == 'k' || c == 'K' || c == 'n' || c == 'N') {
      int step = (c == 'K' || c == 'N') ? 5 : 1;
      s_dump_target_angle -= step;
      if (s_dump_target_angle < 0) s_dump_target_angle = 0;
      calib_save_to_nvs();
      ESP_LOGW(TAG,
               ">>> [CALIB GOC HAT RAC / MO NAP] GIAM GOC MO (-%d do): Hien tai %d do (Pulse: %ld us) (Da luu NVS)",
               step, s_dump_target_angle, (long)angle_to_pulse_us(s_dump_target_angle));
      servo_180_set_angle(s_dump_target_angle);
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
    } else if (c == 'p' || c == 'P') {
      ESP_LOGI(TAG, "[SERIAL] Test MO NAP (%d do)", s_lid_open_angle);
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_LID_OPEN;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (c == 'q' || c == 'Q') {
      ESP_LOGI(TAG, "[SERIAL] Test DONG NAP (%d do)", s_lid_rest_angle);
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_LID_CLOSE;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (c == 'w' || c == 'W') {
      ESP_LOGI(TAG, "[SERIAL] DAO CHIEU SERVO NAP");
      bin_command_t cmd;
      memset(&cmd, 0, sizeof(cmd));
      cmd.type = CMD_LID_INVERT;
      xQueueSend(s_bin_cmd_queue, &cmd, 0);
    } else if (c == 'v' || c == 'V') {
      int step = (c == 'V') ? 5 : 1;
      s_lid_open_angle += step;
      if (s_lid_open_angle > 180) s_lid_open_angle = 180;
      calib_save_to_nvs();
      ESP_LOGW(TAG, ">>> [CALIB SERVO NAP] TANG GOC MO NAP: Hien tai %d do (Da luu NVS)", s_lid_open_angle);
      servo_lid_set_angle(s_lid_open_angle);
    } else if (c == 'z' || c == 'Z') {
      int step = (c == 'Z') ? 5 : 1;
      s_lid_open_angle -= step;
      if (s_lid_open_angle < 0) s_lid_open_angle = 0;
      calib_save_to_nvs();
      ESP_LOGW(TAG, ">>> [CALIB SERVO NAP] GIAM GOC MO NAP: Hien tai %d do (Da luu NVS)", s_lid_open_angle);
      servo_lid_set_angle(s_lid_open_angle);
    } else if (c == 'a' || c == 'A') {
      s_lid_auto_trigger_enabled = !s_lid_auto_trigger_enabled;
      ESP_LOGW(TAG, ">>> [HC-SR04 NAP] Che do tu dong mo nap: %s", s_lid_auto_trigger_enabled ? "BAT (ENABLED)" : "TAT (DISABLED)");
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

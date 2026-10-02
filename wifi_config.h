#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// ==============================================================================
//           CẤU HÌNH WIFI DÙNG CHUNG CHO CẢ 2 BO MẠCH ESP32
//               (ESP32-CAM & ESP32 THƯỜNG / ACTUATOR)
// ==============================================================================
// Mỗi khi đổi mạng WiFi (ở nhà, quán cafe, lớp học, hoặc phát Hotspot 4G từ điện thoại),
// bạn CHỈ CẦN SỬA DUY NHẤT 2 DÒNG DƯỚI ĐÂY trong file này:

#define WIFI_SSID                 "ITF Da Nang"
#define WIFI_PASS                 "itfdanang"

// Ghi đè tự động và tương thích với cả 2 firmware (không bị cảnh báo redefined)
#ifdef CONFIG_ESP_WIFI_SSID
#undef CONFIG_ESP_WIFI_SSID
#endif
#define CONFIG_ESP_WIFI_SSID      WIFI_SSID

#ifdef CONFIG_ESP_WIFI_PASSWORD
#undef CONFIG_ESP_WIFI_PASSWORD
#endif
#define CONFIG_ESP_WIFI_PASSWORD  WIFI_PASS

#endif // WIFI_CONFIG_H

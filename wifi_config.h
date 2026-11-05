#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// ==============================================================================
//           CẤU HÌNH WIFI DÙNG CHUNG CHO CẢ 2 BO MẠCH ESP32
//               (ESP32-CAM & ESP32 THƯỜNG / ACTUATOR)
// ==============================================================================

#define WIFI_SSID                 "TRAM 247 STUDY CAFE & WORKSPACE" // "ITF Da Nang"
#define WIFI_PASS                 "tramloveyou" // "itfdanang"

#ifdef CONFIG_ESP_WIFI_SSID
#undef CONFIG_ESP_WIFI_SSID
#endif
#define CONFIG_ESP_WIFI_SSID      WIFI_SSID

#ifdef CONFIG_ESP_WIFI_PASSWORD
#undef CONFIG_ESP_WIFI_PASSWORD
#endif
#define CONFIG_ESP_WIFI_PASSWORD  WIFI_PASS

#endif // WIFI_CONFIG_H

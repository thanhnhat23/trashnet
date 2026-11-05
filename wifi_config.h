#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// ==============================================================================
//           CẤU HÌNH WIFI DÙNG CHUNG CHO CẢ 2 BO MẠCH ESP32
//               (ESP32-CAM & ESP32 THƯỜNG / ACTUATOR)
// ==============================================================================

#define WIFI_SSID                 "ITF Da Nang" // "Long Nhat" // "TRAM 247 STUDY CAFE & WORKSPACE"
#define WIFI_PASS                 "itfdanang" // "hoichirua" // "tramloveyou"

#ifdef CONFIG_ESP_WIFI_SSID
#undef CONFIG_ESP_WIFI_SSID
#endif
#define CONFIG_ESP_WIFI_SSID      WIFI_SSID

#ifdef CONFIG_ESP_WIFI_PASSWORD
#undef CONFIG_ESP_WIFI_PASSWORD
#endif
#define CONFIG_ESP_WIFI_PASSWORD  WIFI_PASS

#endif // WIFI_CONFIG_H

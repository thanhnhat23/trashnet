#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#define WIFI_SSID                 "Long Nhat"
#define WIFI_PASS                 "hoichirua"

#ifdef CONFIG_ESP_WIFI_SSID
#undef CONFIG_ESP_WIFI_SSID
#endif
#define CONFIG_ESP_WIFI_SSID      WIFI_SSID

#ifdef CONFIG_ESP_WIFI_PASSWORD
#undef CONFIG_ESP_WIFI_PASSWORD
#endif
#define CONFIG_ESP_WIFI_PASSWORD  WIFI_PASS

#endif // WIFI_CONFIG_H

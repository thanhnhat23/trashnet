# TrashNet CAM firmware (ESP32-CAM AI-Thinker)

    idf.py set-target esp32 && idf.py build flash monitor

First boot (no WiFi saved): join `TrashNet-CAM-XXXX`, then `POST http://192.168.4.1/wifi` `{"ssid":"..","pass":"..","key":"optional"}`
(or use the app / web "Devices" page). API: `shared/device-api.md`. Not compiled in the authoring environment: build once and report errors.

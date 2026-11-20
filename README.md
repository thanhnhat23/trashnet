<div align="center">

# ♻️ TrashNet

**Smart waste sorter — ESP32-CAM sees it, YOLO names it, the tray drops it in the right bin.**

</div>

---

## How it works

```
ESP32-CAM ──stream──▶ Server (FastAPI + YOLO) ◀──SSE──▶ Web UI / Flutter app
                              │
                              └──job──▶ ESP32 Actuator ─▶ tray · dump arm · lid · 2× ultrasonic
```

1. An item lands on the tray; the camera frame goes to the server.
2. YOLO detects and classifies it: **Paper · Glass · Other · Plastic**.
3. The actuator rotates to the bin, dumps, and returns the opposite way so cables never wind.

## Features

- 🎯 **YOLO detection** with per-bin scores, so you can see when the model hesitates
- ⚡ **Instant results** — shown right after inference, before the tray finishes moving
- 🤍 **White-on-white** handling via empty-tray reference and a low-threshold re-check
- 🗑️ **Bin-full detection** and hands-free lid on bin 1 (ultrasonic sensors)
- 📡 **Zero-config discovery** — mDNS, UDP beacon and heartbeat
- 📱 **Flutter app** — via the server, or standalone with on-device YOLO

## Quick start

```bat
run_server.bat                    :: http://localhost:8000
run_fake_devices.bat              :: optional: simulated hardware
firmware\flash.bat cam COM5
firmware\flash.bat actuator COM7
ml\train_detect.bat <bbox_dataset> [empty_tray_photos]
```

First boot: join the `TrashNet-CAM-XXXX` / `TrashNet-ACT-XXXX` Wi-Fi and `POST http://192.168.4.1/wifi` with `{"ssid":"…","pass":"…"}`. Devices then show up automatically.

## Hardware

| Part | Pin | Note |
|---|---|---|
| MG996R tray servo (360°) | GPIO 26 | Separate 5–6 V ≥ 3 A supply, common GND |
| MG995 dump servo | GPIO 25 | 0–180° |
| Lid servo (bin 1) | GPIO 27 | |
| Ultrasonic — bin full | TRIG 19 / ECHO 23 | Full at ≤ 3 cm |
| Ultrasonic — lid | TRIG 21 / ECHO 22 | Opens at ≤ 3 cm |

```
          Bin 4 (180°)
               │
Bin 3 (−90°) ──●── Bin 2 (+90°)
               │
          Bin 1 (0°, front)
```

## Project layout

```
server/     FastAPI + web UI (vanilla ES modules, no build step)
mobile/     Flutter app
firmware/   cam/ · actuator/ · tn_common/  (ESP-IDF)
ml/         dataset, training, calibration
shared/     labels, design tokens, device API contract
```

Device API: [`shared/device-api.md`](shared/device-api.md)

## Tests

```bash
cd server && python -m pytest tests -q
python -m pytest ml/tests -q
sh firmware/actuator/test/run_host_tests.sh
```

## License

Released under the [MIT License](LICENSE). Copyright (c) 2017 Gary Thung.

Based on [garythung/trashnet](https://github.com/garythung/trashnet). Third-party components keep their own licenses: [Ultralytics YOLO](https://github.com/ultralytics/ultralytics) (AGPL-3.0), ESP-IDF (Apache-2.0), Flutter (BSD-3-Clause).

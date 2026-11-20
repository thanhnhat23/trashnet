# TrashNet: Kế hoạch refactor, tối ưu IoT, port sang mobile và cải thiện train

> Phạm vi: **chỉ lập kế hoạch**, chưa sửa code. Kế hoạch dựa trên việc rà soát `D:\Project_A\trashnet` ngày 20/11/2026.
> Thứ tự đề xuất: **Phase 0 → 1 → 2 → 3 → 4 → 5**. Phase sau dựa trên API/cấu trúc mà phase trước đã chốt.

---

## 0. Hiện trạng (kết quả rà soát)

### 0.1 Quy mô code

| File | Dòng | Vấn đề chính |
|---|---|---|
| `app.py` | 1405 | Một file làm hết: route, inference, gọi IoT, lịch sử, dataset, **train ngay trong process web**. Dùng nhiều biến global và đọc lại file settings ở mỗi request. |
| `static/js/main.js` | 2924 | Một file duy nhất, khoảng 120 hàm, nhiều timer polling song song, logic trùng lặp. |
| `templates/index.html` | 1364 | 5 view nằm chung một file, 57 inline `style=`, Bootstrap + FontAwesome + Chart.js lấy từ CDN. |
| `static/css/style.css` | 846 | Phong cách glassmorphism, nhiều màu, chưa có design token. |
| `firmware_esp32_actuator/main/main.c` | 1998 | Một file, khoảng 400 dòng menu phím Serial cho debug, comment banner tiếng Việt. |
| `firmware_esp32_cam/main/main.c` | 410 | Còn code chết (auto-upload hardcode `192.168.1.8`). |
| `fusion/predictor.py` | 313 | Bộ lọc khay trống viết tay bằng ngưỡng `std` (dễ vỡ khi đổi ánh sáng). |

### 0.2 Lỗi và điểm bất nhất cần sửa (ưu tiên cao)

1. **Nhãn lớp không thống nhất.** `data/classes.json` = `burnable/glass/metal/plastic` (ResNet), còn web và thùng = `paper/glass/other/plastic`. Mapping `burnable→paper` và `metal→other` đang nằm rải rác ở 3 chỗ (`CLASS_FOLDER_MAP`, `CANONICAL_CLASS_MAP`, `to_canon`). YOLO detect lại dùng 7 lớp khác (`Battery, Bio, Glass, Mask, Metal, Paper, Plastic`).
2. **`/api/info` hiển thị số liệu sai.** Code ghi cứng trọng số 0.70/0.30 và accuracy 99.27%, trong khi `predictor.py` thật dùng **0.60/0.40**. Tên model cũng lẫn lộn giữa "YOLO11s Detect", "YOLO11n-cls" và YOLO detect thật.
3. **Không có LCD trong firmware** (grep `lcd`/`i2c` = 0), nhưng server vẫn gửi `lcd_title` và docstring nhắc "LCD 1602". Đây là code thừa.
4. **Ngưỡng HC-SR04 = 2.0 cm** (`bin1_full_threshold_cm`, `lid_sensor_threshold_cm`) đang **nằm dưới tầm đo tối thiểu (~2 cm)** của cảm biến, nên kết quả đo không tin cậy.
5. **Servo 360 chạy theo thời gian (open-loop).** Không có cảm biến vị trí Home nên sai số tích lũy dần, mâm lệch ngăn sau nhiều chu kỳ. Đây là nguyên nhân chính khiến IoT "không trơn tru".
6. **ESP32-CAM:** chỉ retry WiFi 4 lần rồi chuyển sang AP mode và **không bao giờ quay lại STA**. Ảnh chụp ở **QVGA 320×240, jpeg_quality 14**, nên ảnh mờ và vỡ nét.
7. **Auto-IP bằng UDP broadcast chỉ gửi 3 gói lúc có IP.** Nếu server khởi động sau ESP32 thì sẽ **không bao giờ nhận được**. Mạng có client isolation (WiFi trường/công ty) cũng chặn broadcast.
8. **`/api/predict` gọi actuator đồng bộ** (timeout 2 s) ngay trong request, nên response bị chậm theo IoT.
9. **Vòng auto-scan nằm ở trình duyệt** (`setInterval` 3.2 s, polling `/api/actuator/status` mỗi 800 ms). Đóng tab là hệ thống dừng, mở 2 tab là chụp/hất trùng.
10. **Stream proxy:** mỗi client mở một kết nối riêng tới ESP32-CAM. CAM chỉ chịu được 1–2 stream nên web và app cùng mở sẽ nghẽn.
11. **Lịch sử lưu JSON kèm thumbnail base64**, và cả file bị ghi lại ở mỗi lần dự đoán (khoảng 82 KB, tối đa 300 bản ghi). Thống kê đếm theo **so khớp chuỗi tiếng Việt** (`'giấy' in name_vi`).
12. **Bảo mật:** WiFi SSID/password nằm trong `wifi_config.h` và `main.c` (AP mode). API không có xác thực nên ai trong LAN cũng điều khiển được servo. Nút flash LED gọi thẳng IP ESP32 từ trình duyệt (`no-cors`), không đi qua server.
13. **Model YOLO được load 2 lần** lúc import (`load_yolo()` trong lệnh `print`).
14. **Confusion matrix của YOLO detect** (`yolo_detect/results`) cho thấy **rất nhiều false positive trên background** (133/74/90/115/140/47/137 box nhận nhầm nền). Vì vậy mới phải có hack `is_empty_box`. Cần sửa tận gốc bằng dữ liệu (xem Phase 4).
15. `README.md` đã lỗi thời: còn ghi 8 lớp, 96.25%, Lua/Torch7, port 5000 (thực tế là 5001).

### 0.3 Hàm và endpoint thừa hoặc trùng

- **JS không được gọi ở đâu:** `switchCameraSource`, `stopWebcam`, `renderAnalyticsStats`, `stepUltrasonicThreshold`.
- **JS trùng chức năng:** `connectActuator` / `connectActuatorFromMain`; `rotateToAngle` / `triggerManualBin`; `simulateRotaryCycle` chỉ là animation giả.
- **API trùng:** `/api/actuator/config` và `/api/actuator/settings`; `/api/device/register` và UDP listener (cùng logic, viết 2 lần).
- **Firmware actuator:** khoảng 400 dòng menu phím Serial (`getchar` ở dòng ~1628 đến ~1950) và các `ESP_LOGI` hướng dẫn phím dài.
- **Firmware CAM:** `auto_upload_task`, `/autoupload`, `ENABLE_AUTO_UPLOAD` (đang tắt, URL hardcode).

---

## Phase 0. Dọn cấu trúc và file thừa

### 0.A Danh sách xóa hoặc di chuyển

| Đường dẫn | Hành động | Lý do |
|---|---|---|
| `__pycache__/`, `data/__pycache__/`, `fusion/__pycache__/` | Xóa | Sinh tự động |
| `firmware_*/build/`, `firmware_*/.cache/`, `firmware_esp32_cam/managed_components/` | Xóa khỏi repo (giữ local nếu cần) | Build artifact, đã có trong `.gitignore` |
| `firmware_esp32_cam/sdkconfig.old` | Xóa | Bản sao lưu |
| `firmware_esp32_cam/build_and_flash.ps1` | Xóa (giữ `.bat`), hoặc gộp thành `scripts/flash.py` | Trùng |
| `data/dataset-resized.zip` (42 MB) | Xóa | Trùng thư mục đã giải nén |
| `data/train.py`, `data/predict.py`, `data/constants.py`, `data/resize.py`, `data/split_data.py` | Xóa | Pipeline 8 lớp cũ, đã có bản 4 lớp thay thế |
| `yolo/`, `test_yolo/*.py`, `test_fusion.py` | Xóa | Script thử nghiệm cũ |
| `test_yolo/<ảnh mẫu>` | Chuyển sang `server/static/samples/` | Route `/test_yolo/<file>` đang dùng |
| `predict_resnet.py`, `soft_voting.py`, `weight_search.py`, `evaluate_fusion.py` | Chuyển sang `ml/eval/` và gộp lại | Công cụ đánh giá, không thuộc server |
| `runs/classify/train`, `train-2` | Xóa (giữ `yolo11n_pbl4` nếu còn dùng) | Thí nghiệm cũ |
| `yolo11n-cls.pt`, `weights/yolo26n.pt` | Xóa (Ultralytics tự tải lại). Grep xác nhận `yolo26n` không còn được tham chiếu trước khi xóa | Trọng số pretrained gốc |
| `models/yolo11n_trashnet_best.pt` | Giữ tạm. Bỏ hẳn nếu Phase 4 bỏ YOLO-cls | Fallback |
| `classification_history.json`, `actuator_settings.json` | Chuyển sang `server/var/` và thêm vào gitignore | Dữ liệu runtime |
| `wifi_config.h` | Xóa khỏi repo, thay bằng provisioning (Phase 2) | Lộ mật khẩu |
| README phần Lua/Torch7 | Xóa, viết lại README | Lỗi thời |

> Firmware đang `#include "wifi_config.h"` qua `INCLUDE_DIRS "../.."`. Khi xóa file này phải sửa đồng thời `CMakeLists.txt`.

### 0.B Cấu trúc thư mục đích

```
trashnet/
├─ shared/
│  ├─ labels.json            # Single source of truth: 4 bins, colors, vi/en names
│  ├─ design-tokens.json     # Colors, spacing, typography → CSS vars + Flutter theme
│  └─ api-contract.md        # REST + SSE contract (v1)
├─ server/
│  ├─ main.py                # App factory + run
│  ├─ config.py              # Settings (env + server/var/settings.json)
│  ├─ api/                   # Thin route modules (blueprints/routers)
│  │  ├─ predict.py  devices.py  actuator.py  history.py
│  │  ├─ stream.py   events.py   dataset.py   system.py
│  ├─ services/
│  │  ├─ inference.py        # Detector + classifier pipeline
│  │  ├─ orchestrator.py     # Sorting state machine (capture → infer → dispatch)
│  │  ├─ actuator_client.py  # HTTP client + retry + job tracking
│  │  ├─ camera_client.py    # Capture + single-upstream MJPEG fan-out
│  │  ├─ discovery.py        # mDNS browse/advertise + heartbeat registry
│  │  ├─ event_bus.py        # SSE pub/sub
│  │  └─ history_repo.py     # SQLite
│  ├─ domain/                # Dataclasses/schemas, label mapping
│  ├─ web/                   # Web UI (see Phase 1)
│  └─ var/                   # Runtime data (gitignored)
├─ ml/
│  ├─ configs/               # YAML per experiment
│  ├─ data/                  # Raw / processed (gitignored), split scripts
│  ├─ train_detector.py  train_classifier.py  augment.py
│  ├─ eval/                  # evaluate.py, weight_search.py, reports
│  └─ export.py              # → server/models/
├─ firmware/
│  ├─ common/                # wifi_prov, mdns, heartbeat, ota (ESP-IDF components)
│  ├─ cam/                   # ESP32-CAM project
│  └─ actuator/              # ESP32 actuator project
├─ mobile/                   # Flutter app (Phase 3)
└─ scripts/                  # flash.py, monitor.py, find_devices.py
```

---

## Phase 1. Refactor web và server

### 1.1 Server (Python)

**Quyết định framework:** giữ **Flask** và tách thành Blueprint là ít rủi ro nhất. Nếu muốn có async, SSE gọn và tự sinh OpenAPI cho app mobile, chuyển sang **FastAPI**. Đề xuất chọn **FastAPI**, vì app Flutter sẽ hưởng lợi trực tiếp từ OpenAPI schema (có thể sinh client tự động).

Việc cần làm:

1. **`shared/labels.json`** là nguồn duy nhất cho 4 ngăn: `id, bin, name_vi, name_en, color, tips`. Xóa `WASTE_META`, `BIN_MAPPING`, `CLASS_FOLDER_MAP`, `CANONICAL_CLASS_MAP`, `to_canon`. Mapping từ lớp thô của model sang ngăn nằm duy nhất trong `domain/labels.py`.
2. **Response của `/predict` gọn lại.** Bỏ các field màu/icon/badge lặp lại; client tự tra `labels.json`. Ví dụ:
   ```json
   { "id": "...", "has_object": true, "label": "plastic", "bin": 4,
     "confidence": 0.93, "boxes": [...], "models": {"detector": {...}, "classifier": {...}},
     "job_id": "a1b2" }
   ```
3. **Orchestrator chạy ở server** (thay cho vòng lặp trong trình duyệt). Chi tiết ở mục 2.4.
4. **Gửi lệnh IoT bất đồng bộ.** `/predict` trả kết quả ngay. Lệnh tới actuator đi qua queue và tiến độ được đẩy qua SSE.
5. **Thay polling bằng SSE `/api/v1/events`.** Các event gồm `device`, `prediction`, `job`, `sensor`. Server có **một** background task poll `/status` của actuator (hoặc nhận push từ actuator) rồi phát cho mọi client.
6. **MJPEG fan-out:** server giữ 1 kết nối upstream tới CAM và chia frame cho N client (web + app).
7. **Lịch sử chuyển sang SQLite** (`history` table). Thumbnail lưu thành file `var/thumbs/<id>.jpg`. Thống kê dùng `GROUP BY bin`.
8. **Settings:** một module `config.py` load một lần và cache, có ghi nguyên tử (atomic write). Bỏ fallback IP hardcode (`172.16.x.x`) đang nằm ở 6 chỗ trong server và JS.
9. **Gộp API:** `/actuator/config` vào `/actuator/settings`; `/device/register` và UDP vào `discovery.register()`.
10. **Bỏ train trong process web** (`background_trainer` đang trùng với `train_resnet_4class.py`). Nếu vẫn muốn bấm nút train trên web, server chạy `ml/train_classifier.py` bằng `subprocess` và stream log. Bỏ `best_val_acc` hardcode 99.27.
11. **`/api/info`** đọc từ `server/models/metrics.json` do `ml/export.py` sinh ra, không ghi cứng nữa.
12. **Bảo mật cơ bản:** API token (header `X-Api-Key`) cho các route điều khiển. Firmware chỉ nhận lệnh có shared key.
13. **Cache-Control:** chỉ áp `no-store` cho `/api/*`. File static dùng hash/version.
14. **Logging:** dùng module `logging` với tag `[infer] [iot] [disc]` bằng tiếng Anh, bỏ `print` tiếng Việt không dấu.

**Chuẩn comment** (áp dụng cho Python, JS, C):
- Xóa toàn bộ comment banner `# ====`, comment kể lại code và comment tiếng Việt.
- Chỉ giữ docstring hoặc comment **1–2 dòng bằng tiếng Anh** ở hàm public hoặc hàm có logic không hiển nhiên. Ví dụ:
  ```python
  def is_empty_tray(frame, ref):
      """Return True when frame differs from the empty-tray reference by < threshold."""
  ```
  ```c
  /* Rotate tray from current bin to target; blocks until home sensor or timeout. */
  ```

### 1.2 Web UI: minimalist dạng text

**Nguyên tắc thiết kế** (dùng chung cho web và mobile):
- Nền trung tính (trắng / `#0a0a0a` khi dark), 1 màu chữ chính và 1 màu phụ. **Màu chỉ dùng cho 4 ngăn rác**, để màu luôn mang ý nghĩa.
- Font: Inter cho chữ, JetBrains Mono cho số liệu (ms, cm, %).
- Không glassmorphism, không gradient, không shadow lớn. Viền 1px, bo góc 8px, lưới khoảng cách 4/8px.
- Icon tối thiểu: chỉ dùng ở thanh điều hướng. Kết quả hiển thị dạng chữ lớn, ví dụ `NHỰA · Ngăn 4 · 93%`.
- Trạng thái thiết bị hiển thị dạng chữ và chấm màu: `● CAM online  ● Actuator busy (job a1b2)`.

**Bản đồ màn hình** (giống nhau trên web và app):

| Màn | Nội dung |
|---|---|
| **Live** | Stream + box, kết quả gần nhất, nút *Chụp*, công tắc *Auto*, chọn model |
| **History** | Danh sách lọc theo ngăn, tìm kiếm, xóa |
| **Stats** | 4 con số theo ngăn, biểu đồ theo ngày, độ tin cậy trung bình |
| **Devices** | CAM/Actuator: trạng thái, IP (tự phát hiện), RSSI, firmware; nút test ngăn 1–4, hất, nắp |
| **Settings** | Hiệu chuẩn servo (timing, góc), ngưỡng siêu âm, chu kỳ auto, API key |

**Tách component cho web**:
- **Phương án A (đề xuất):** vanilla **ES modules + Vite**, không cần framework. Gọn và dễ bảo trì.
  ```
  server/web/src/
  ├─ main.js               # bootstrap + router
  ├─ api/client.js         # fetch wrapper + SSE subscribe
  ├─ state/store.js        # tiny pub/sub store
  ├─ components/           # StatusDot, ResultCard, BinBadge, Stepper, DeviceRow, HistoryItem, Chart
  ├─ views/                # live.js, history.js, stats.js, devices.js, settings.js
  └─ styles/tokens.css     # generated from shared/design-tokens.json
  ```
- **Phương án B:** dùng **Flutter Web** cho cả web, thay hẳn UI hiện tại (xem Phase 3). Một codebase duy nhất, giao diện giống hệt, nhưng tải lần đầu nặng hơn.
- Bỏ Bootstrap và FontAwesome CDN (chỉ dùng vài icon SVG inline). Chart.js chỉ giữ cho màn Stats.
- Mỗi file JS nên dưới 300 dòng, mỗi view tự quản lý timer của mình và hủy khi rời view.

---

## Phase 2. IoT: tự động kết nối và luồng hoạt động mượt

### 2.1 Cấp WiFi không hardcode (provisioning)

Dùng component `wifi_provisioning` của ESP-IDF:
- **Lần đầu nạp code:** chưa có WiFi trong NVS thì ESP32 phát **SoftAP `TrashNet-CAM-XXXX` / `TrashNet-ACT-XXXX`** (XXXX = 4 ký tự cuối MAC) kèm captive portal, hoặc dùng **BLE provisioning** (app ESP SoftAP/BLE Prov của Espressif, hoặc tích hợp vào app Flutter sau này).
- Sau khi người dùng nhập SSID/password, thông tin được lưu vào NVS và thiết bị reboot sang STA.
- Giữ nút BOOT 5 s để xóa NVS WiFi và vào lại chế độ provisioning.
- Khi STA rớt mạng: reconnect với **exponential backoff** (1s → 2s → … → 60s), **không** chuyển hẳn sang AP mode như CAM hiện tại.

### 2.2 Tự động tìm IP (bỏ nhập tay)

Kết hợp 3 tầng, tầng sau là dự phòng cho tầng trước:

1. **mDNS / DNS-SD (chính).**
   - ESP32 dùng component `espressif/mdns`, hostname `trashnet-cam.local` / `trashnet-act.local`.
   - Quảng bá service `_trashnet._tcp` với TXT `role=cam|actuator, id=<mac>, fw=<ver>`.
   - Server dùng thư viện `zeroconf` để browse `_trashnet._tcp`, tự cập nhật IP khi thiết bị đổi IP.
   - Server cũng **tự quảng bá** `_trashnet-server._tcp`, để ESP32 và app mobile tìm ngược ra server.
2. **Heartbeat HTTP (đảm bảo, không phụ thuộc thứ tự khởi động).**
   - ESP32 resolve server qua mDNS rồi gửi `POST /api/v1/devices/heartbeat` mỗi **15 s** với nội dung `{role, id, ip, fw, rssi, uptime, state}`.
   - Server đánh dấu thiết bị offline nếu quá 45 s không nhận heartbeat và phát event `device` qua SSE.
   - Cách này khắc phục lỗi "server bật sau thì không biết IP".
3. **UDP broadcast (fallback).** Giữ listener port 5005, nhưng ESP32 gửi **định kỳ mỗi 30 s** thay vì chỉ 3 gói lúc boot. Server khi khởi động cũng gửi `TRASHNET:DISCOVER` để thiết bị trả lời ngay.

Ghi chú vận hành:
- Mạng có **client isolation** (WiFi trường/công ty) chặn cả mDNS lẫn broadcast. Khi đó dùng router riêng hoặc hotspot, hoặc đặt **DHCP reservation**, hoặc nhập IP tay (vẫn giữ ô nhập tay trong Settings làm phương án cuối).
- Windows 10/11 resolve `.local` được nhưng không ổn định. Server không nên dựa vào hostname mà nên dùng IP lấy từ kết quả browse của `zeroconf`.
- `find_esp_devices.py` (quét subnet) chuyển sang `scripts/find_devices.py` làm công cụ chẩn đoán, không còn nằm trong luồng chính.

### 2.3 App và web tìm server

- **Web** được server phục vụ nên không cần tìm.
- **App mobile** browse `_trashnet-server._tcp` (Android NSD / iOS Bonjour). Dự phòng: quét QR hiển thị trên trang Settings của web (chứa `http://<ip>:5001` và API key).
- **App chỉ nói chuyện với server, không gọi thẳng ESP32.** Server là gateway duy nhất, nhờ đó tránh xung đột lệnh và tránh nghẽn stream.

### 2.4 Luồng phân loại: state machine ở server

```
IDLE ──(lid sensor event | manual | auto timer)──► CAPTURE (3 frames, flash on)
CAPTURE ──► CHECK (blur + empty-tray) ──fail──► IDLE (or retry once)
CHECK ──► INFER (multi-frame vote) ──low conf──► BIN "other" or ask user
INFER ──► DISPATCH (POST /action, job_id) ──► WAIT_DONE (SSE/status job_id)
WAIT_DONE ──done──► COOLDOWN (N s) ──► IDLE
WAIT_DONE ──timeout 15s──► ERROR (alert, auto-home) ──► IDLE
```

- Chỉ **một** job được chạy tại một thời điểm, có khóa ở server. Nhiều client bấm cùng lúc cũng không bị hất trùng.
- Trigger ưu tiên: **cảm biến siêu âm ở khay → actuator push event → server chụp**, thay cho việc chụp định kỳ mù.

### 2.5 Firmware actuator: điều khiển trơn tru

| Vấn đề | Giải pháp |
|---|---|
| Servo 360 quay theo thời gian, lệch dần | Thêm **cảm biến Home** (hall + nam châm, hoặc công tắc hành trình / IR) để homing sau mỗi chu kỳ. Tốt hơn nữa: đổi sang **stepper 28BYJ-48/NEMA17 + driver**, hoặc servo định vị. Trong lúc chưa đổi phần cứng, homing bằng cảm biến sau mỗi lần về ngăn 1. |
| Giật khi khởi động/dừng | Ramp tốc độ (1500 → 1750 µs qua 100–150 ms) và ramp ngược khi dừng. |
| Servo rung khi đứng yên | Ngắt PWM (duty 0) sau khi đến vị trí với servo 180 hất rác và nắp. |
| `s_is_busy` dùng chung giữa nhiều task | Gom vào một **state machine** trong `actuator_worker_task`, trạng thái đọc qua getter có mutex hoặc atomic. |
| Lệnh bị xếp hàng ngầm | `/action` trả `202 {job_id}` nếu rảnh, `409 busy` nếu đang chạy. `/status` trả `{state, job_id, step, position, homed, sensors}`. |
| HC-SR04 ngưỡng 2 cm, nhiễu | Đặt ngưỡng ≥ 4–5 cm, **median 5 mẫu**, hysteresis (vào ≤ T, ra ≥ T+1 cm). Đo xen kẽ 2 cảm biến cách nhau ≥ 60 ms để tránh nhiễu chéo. |
| Tắt brownout (`RTC_CNTL_BROWN_OUT_REG = 0`) | Bật lại brownout. Cấp **nguồn 5V ≥ 3A riêng cho servo**, nối chung GND, tụ 470–1000 µF gần servo. Tắt brownout chỉ là che triệu chứng. |
| Menu phím Serial khoảng 400 dòng | Chuyển sang `debug_console.c`, bật qua Kconfig `TRASHNET_DEBUG_CONSOLE` (mặc định tắt). |
| `main.c` 2000 dòng | Tách: `wifi_prov.c`, `net_discovery.c` (mdns + heartbeat), `http_api.c`, `servo.c`, `tray.c` (rotation + homing), `dumper.c`, `lid.c`, `ultrasonic.c`, `calib_store.c` (NVS), `app_main.c`. |
| Không cập nhật firmware từ xa | Thêm **OTA** qua HTTP (`/ota`, server cung cấp file `.bin`). Partition CAM đã có `otadata` nhưng mới có `factory`, cần thêm `ota_0`/`ota_1`. |

### 2.6 Firmware ESP32-CAM

- Xóa `auto_upload_task`, `/autoupload`, `ENABLE_AUTO_UPLOAD`. Flash LED đi qua server (`/api/v1/camera/flash`).
- `/capture` hỗ trợ `?res=svga&q=10&flash=1&warmup=2` (bỏ 1–2 frame đầu sau khi bật flash để AEC/AWB ổn định).
- Stream giữ ở VGA, capture để nhận diện dùng **SVGA 800×600 hoặc XGA**, `jpeg_quality 10–12`, `fb_count 2` trên PSRAM.
- Cấu hình sensor: `set_sharpness`, `set_denoise`, `set_gainceiling`, AEC/AWB bật, `aec2`, `lenc` (lens correction) bật.
- Tách module: `camera.c`, `http_api.c` và dùng chung `firmware/common` (wifi_prov, mdns, heartbeat, ota).

---

## Phase 3. Port web sang app mobile

### 3.1 So sánh lựa chọn

| Tiêu chí | **Flutter** | React Native (Expo) | Kotlin (Jetpack Compose / Compose Multiplatform) |
|---|---|---|---|
| Nền tảng | Android, iOS, **Web**, Desktop từ 1 codebase | Android, iOS, Web (react-native-web) | Android native; CMP có iOS/Desktop/Web (web còn beta) |
| UI giống hệt web | **Rất tốt**: tự render, pixel-identical giữa các nền tảng | Tốt, nhưng component native có khác biệt nhỏ | Tốt trên Android, iOS/Web kém trưởng thành hơn |
| Tái dùng code web hiện tại | Không (Dart) | Một phần logic JS | Không |
| MJPEG stream | Package `flutter_mjpeg` hoặc tự parse multipart | `react-native-webview` hoặc thư viện | Tự parse multipart / Coil |
| mDNS / NSD | `nsd`, `multicast_dns` | `react-native-zeroconf` (cần native module, không chạy Expo Go) | `NsdManager` sẵn có |
| BLE provisioning ESP32 | `esp_provisioning` / `flutter_blue_plus` | `react-native-ble-plx` | Thư viện chính thức của Espressif (Android) |
| Hiệu năng stream + chart | Tốt | Khá | Tốt nhất trên Android |
| Học phí | Dart mới, nhưng dễ | Thấp nếu đã biết JS | Thấp nếu đã quen Kotlin |

### 3.2 Đề xuất: **Flutter**

Lý do:
1. Yêu cầu **"design web và mobile tương đương"**: Flutter tự render nên giao diện giống hệt nhau, và có thể **dùng chính Flutter Web để thay web UI** về sau (Phương án B ở 1.2), tức một codebase cho cả 3 nền tảng.
2. Có sẵn thư viện cho mDNS, MJPEG và provisioning ESP32. Build iOS và Android cùng lúc.
3. App chủ yếu là UI + REST/SSE, không cần tính năng native sâu, nên lợi thế của Kotlin native không đáng kể.

> **Khi nào chọn khác:** chỉ cần Android và nhóm đã quen Kotlin thì chọn **Jetpack Compose**. Muốn tái dùng tối đa JS và chỉ cần app nhanh thì chọn **React Native/Expo** (nhưng mDNS cần dev build, không chạy Expo Go).

### 3.3 Kiến trúc app Flutter

```
mobile/lib/
├─ core/
│  ├─ api/          # REST client (generated from OpenAPI), SSE stream
│  ├─ discovery/    # mDNS browse _trashnet-server._tcp, QR fallback
│  ├─ theme/        # generated from shared/design-tokens.json
│  └─ labels.dart   # generated from shared/labels.json
├─ features/
│  ├─ live/  history/  stats/  devices/  settings/  provisioning/
└─ widgets/         # StatusDot, ResultCard, BinBadge, Stepper, DeviceRow...
```

- State management: **Riverpod**.
- `shared/design-tokens.json` sinh ra `tokens.css` (web) và `app_theme.dart` (app) bằng một script nhỏ, để màu, cỡ chữ và khoảng cách luôn đồng bộ.
- Màn **Provisioning** trong app: quét BLE/SoftAP ESP32 để cấp WiFi, thay app Espressif.
- iOS: khai báo `NSLocalNetworkUsageDescription` và `NSBonjourServices` (`_trashnet-server._tcp`), nếu không iOS sẽ chặn mDNS.
- Android: quyền `INTERNET`, `CHANGE_WIFI_MULTICAST_STATE`, và `usesCleartextTraffic` cho HTTP trong LAN.

### 3.4 Lộ trình port

1. Chốt **API v1 + OpenAPI** (cuối Phase 1). Đây là điều kiện bắt buộc.
2. Theme, discovery và màn Live (stream + kết quả qua SSE).
3. Devices + Settings (hiệu chuẩn).
4. History + Stats.
5. Provisioning BLE/SoftAP.
6. (Tùy chọn) Build Flutter Web và thay `server/web`.

---

## Phase 4. Train lại để nhận diện tốt trên camera mờ

### 4.1 Nguyên nhân hiện tại

- **Domain gap:** ResNet train trên ảnh TrashNet/dataset studio (sắc nét, nền trắng, iPhone). YOLO detect train trên dataset Kaggle `WASTE.v1i`. Trong khi ảnh thực tế là **OV2640 QVGA, JPEG nén mạnh, ánh sáng flash**.
- **ResNet phân loại cả khung hình** chứ không phân loại vùng vật thể, nên nền chiếm phần lớn input.
- **YOLO có nhiều false positive trên nền** (confusion matrix), dẫn đến phải dùng hack `is_empty_box`.
- Augmentation yếu (chỉ flip, rotate 15°, jitter 0.1). Không có blur, nhiễu hay nén JPEG.
- `Resize((224,224))` làm méo tỉ lệ 4:3. Phần val transform bị lặp `Resize` 2 lần (vô hại nhưng thừa).

### 4.2 Pipeline mới (2 tầng)

```
Frame (SVGA) ─► Quality gate ─► Detector (YOLO11n/s, 1 lớp "object" hoặc 4 lớp)
             (blur/empty)        │ crop top box + 10% padding
                                 ▼
                         Classifier (4 bins) on crop  ─► multi-frame vote ─► bin
```

1. **Quality gate (trước khi chạy model)**
   - Đo độ nét bằng **variance of Laplacian**. Dưới ngưỡng thì chụp lại (tối đa 2 lần).
   - Phát hiện khay trống bằng cách **so với ảnh khay trống tham chiếu** (chụp khi idle, cập nhật định kỳ): `absdiff` + ngưỡng diện tích. Cách này thay cho các ngưỡng `std` viết tay.
2. **Detector:** train lại YOLO11n (hoặc 11s nếu GPU cho phép) trên **ảnh chụp từ chính ESP32-CAM đặt trên khay**, kèm **10–20% ảnh khay trống không có nhãn** (background images) để giảm false positive. Có thể dùng một lớp `object` (detector chỉ lo vị trí) để đơn giản.
3. **Classifier trên crop:** EfficientNet-B0 / MobileNetV3-Large / ConvNeXt-Tiny (so sánh với ResNet18 làm baseline). Input 224 **letterbox** (giữ tỉ lệ). Train **trực tiếp 4 lớp ngăn** `paper, glass, other, plastic`, bỏ `burnable/metal`.
4. **Fusion:** nếu vẫn giữ 2 model, tìm trọng số bằng `weight_search` **trên tập val thật từ camera**, không dùng tập studio.
5. **Multi-frame voting:** chụp 3 frame, cộng trung bình xác suất. Nếu confidence < τ (ví dụ 0.6) thì đưa vào ngăn `other` hoặc yêu cầu xác nhận trên UI.
6. **Calibration:** temperature scaling trên tập val để confidence phản ánh đúng xác suất, giúp ngưỡng τ có ý nghĩa.

### 4.3 Dữ liệu

| Bước | Chi tiết |
|---|---|
| Thu thập domain thật | Script `ml/collect.py` gọi `/capture` của CAM, lưu ảnh khi có vật trên khay. Mục tiêu **≥ 300 ảnh/ngăn** chụp thật, đa dạng góc, độ nhàu, ánh sáng, có và không có flash. |
| Gán nhãn | Label Studio hoặc Roboflow. Có thể pre-label bằng model hiện tại rồi sửa tay. |
| Tận dụng History | Mỗi lần web/app hiển thị kết quả có nút "Sai? → chọn ngăn đúng". Ảnh gốc được lưu vào `ml/data/feedback/` (active learning). |
| Chia tập | Chia theo **phiên chụp/vật thể** chứ không chia ngẫu nhiên từng ảnh, để tránh rò rỉ giữa train và val. Tập **test chỉ gồm ảnh camera thật**. |
| Cân bằng lớp | `WeightedRandomSampler` hoặc class weight. Lớp `other` vốn gộp nhiều loại nên cần nhiều mẫu hơn. |

### 4.4 Augmentation mô phỏng camera mờ (Albumentations)

```python
A.Compose([
    A.LongestMaxSize(256), A.PadIfNeeded(256, 256), A.RandomCrop(224, 224),
    A.OneOf([A.GaussianBlur((3, 7)), A.MotionBlur(7), A.Defocus((2, 4))], p=0.5),
    A.Downscale(scale_range=(0.3, 0.6), p=0.4),            # simulate QVGA upscale
    A.ImageCompression(quality_range=(15, 50), p=0.6),      # OV2640 JPEG artifacts
    A.GaussNoise(p=0.3), A.ISONoise(p=0.2),
    A.RandomBrightnessContrast(0.3, 0.3, p=0.6),
    A.RandomGamma(p=0.3), A.HueSaturationValue(10, 20, 10, p=0.4),
    A.CLAHE(p=0.1), A.HorizontalFlip(), A.Rotate(25, p=0.5),
    A.CoarseDropout(p=0.2),
])
```

- YOLO: bật `mosaic`, `mixup 0.1`, `hsv_*`, `degrees 15`, `blur` và thêm Albumentations callback như trên.
- Train classifier: AdamW + cosine + warmup, label smoothing 0.1, **mixup/cutmix**, fine-tune 2 giai đoạn (freeze backbone 3 epoch rồi unfreeze), early stopping theo **macro-F1** trên tập val thật.
- Test-time: TTA nhẹ (gốc + flip) nếu latency cho phép.

### 4.5 Đánh giá và chấp nhận

- Báo cáo `ml/eval/report.md`: accuracy, **macro-F1**, confusion matrix 4 ngăn, **chia theo mức blur** (Laplacian bins), FP rate trên khay trống, latency CPU/GPU.
- Tiêu chí ra mắt: macro-F1 tập camera thật ≥ baseline + 10 điểm, FP khay trống < 1%, latency end-to-end (chụp → lệnh) < 1.5 s.
- `ml/export.py` copy weights + `metrics.json` + `labels.json` sang `server/models/`, rồi server hot-reload.

### 4.6 Phần cứng hỗ trợ độ nét (chi phí thấp, hiệu quả cao)

- **Chỉnh tiêu cự ống kính OV2640** (vặn ống kính, lần đầu cần cạy keo) cho đúng khoảng cách khay, thường 10–20 cm. Đây là việc mang lại cải thiện độ nét lớn nhất.
- Cân nhắc module **OV5640 autofocus** hoặc ống kính góc hẹp.
- Ánh sáng cố định: dải LED trắng khuếch tán thay cho flash điểm (flash GPIO4 gây lóa và cháy sáng).
- Nền khay màu trung tính, không bóng, giúp phát hiện khay trống ổn định.

---

## Phase 5. Kiểm thử và vận hành

- **Server:** pytest cho `domain/labels`, `orchestrator` (giả lập actuator/CAM bằng mock HTTP), `discovery`.
- **Simulator thiết bị:** `scripts/fake_devices.py` giả CAM (trả ảnh mẫu) và actuator (trả job), để dev web/app không cần phần cứng.
- **Firmware:** test homing 50 chu kỳ liên tục, ghi số lần lệch ngăn. Mục tiêu 0 lần lệch.
- **Soak test:** chạy auto 2 giờ, theo dõi memory ESP32 (`heap_caps_get_free_size` gửi trong heartbeat), số lần reconnect, độ trễ.
- Viết lại `README.md`: cài đặt, flash + provisioning, chạy server, mở app.

---

## Checklist tổng hợp

**Phase 0: Dọn dẹp**
- [ ] Xóa/di chuyển file theo bảng 0.A
- [ ] Tạo cấu trúc thư mục mới, `shared/labels.json`, `shared/design-tokens.json`
- [ ] Cập nhật `.gitignore` (thêm `server/var/`, `ml/data/`, `*.pt` ngoài `server/models/`)

**Phase 1: Server + Web**
- [ ] Tách `app.py` thành `api/` + `services/` + `domain/`
- [ ] Thống nhất nhãn 4 ngăn, bỏ mapping rải rác
- [ ] Orchestrator state machine + job lock
- [ ] SSE `/api/v1/events`, bỏ polling ở client
- [ ] MJPEG fan-out một upstream
- [ ] SQLite history + thumbnail file
- [ ] `/api/info` đọc từ `metrics.json`
- [ ] API key cho route điều khiển
- [ ] Web: ES modules + Vite, 5 view, component hóa, UI minimalist
- [ ] Xóa hàm JS thừa/trùng (mục 0.3)
- [ ] Chuẩn comment: docstring tiếng Anh ngắn, bỏ banner

**Phase 2: IoT**
- [ ] `firmware/common`: wifi_prov, mdns, heartbeat, ota
- [ ] Provisioning SoftAP/BLE, xóa `wifi_config.h` và AP password hardcode
- [ ] mDNS `_trashnet._tcp` + heartbeat 15 s + UDP fallback định kỳ
- [ ] Actuator: tách module, state machine, `/action` → 202/409, `/status` có `job_id`
- [ ] Cảm biến Home + homing; ramp servo; ngắt PWM khi nghỉ
- [ ] HC-SR04 median + hysteresis, ngưỡng ≥ 4 cm
- [ ] Bật lại brownout, tách nguồn servo
- [ ] Debug console sau Kconfig flag
- [ ] CAM: bỏ auto-upload, capture SVGA q10–12, warmup frame, reconnect backoff

**Phase 3: Mobile (Flutter)**
- [ ] OpenAPI → Dart client
- [ ] Theme sinh từ design tokens
- [ ] Discovery mDNS + QR fallback
- [ ] Live → Devices/Settings → History/Stats → Provisioning
- [ ] (Tùy chọn) Flutter Web thay web UI

**Phase 4: ML**
- [ ] `ml/collect.py` thu ảnh thật từ ESP32-CAM
- [ ] Gán nhãn, chia tập theo phiên chụp
- [ ] Detector có background images; classifier trên crop 4 ngăn
- [ ] Augmentation mô phỏng blur/JPEG/noise
- [ ] Quality gate (Laplacian + empty-tray reference)
- [ ] Multi-frame vote + temperature scaling + ngưỡng "uncertain"
- [ ] Báo cáo đánh giá theo mức blur; export + hot-reload
- [ ] Chỉnh tiêu cự ống kính, ánh sáng khuếch tán

**Phase 5: Kiểm thử**
- [ ] Unit test server, simulator thiết bị
- [ ] Test homing 50 chu kỳ, soak test 2 giờ
- [ ] README mới

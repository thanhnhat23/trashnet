# 📷 TrashNet ESP32-CAM Firmware (ESP-IDF)

Hệ thống Firmware chuẩn **ESP-IDF** (Espressif IoT Development Framework) dành cho vi điều khiển **ESP32-CAM (AI-Thinker OV2640)** để truyền stream video MJPEG và chụp ảnh độ phân giải cao phục vụ mô hình AI phân loại rác thải TrashNet.

---

## 📌 1. Các tính năng chính

- **Chuẩn ESP-IDF v4.4 / v5.x:** Tối ưu hóa hiệu năng phần cứng, tận dụng bộ nhớ mở rộng **PSRAM (SPIRAM 4MB)** để xử lý ảnh mượt mà, không bị tràn RAM.
- **MJPEG Video Streaming (`/stream`):** Truyền hình ảnh trực tiếp thời gian thực về giao diện Web Dashboard TrashNet.
- **Snapshot JPEG (`/capture`):** Bắt khung hình tĩnh chất lượng cao gửi đến server phân tích Late Fusion (ResNet18 + YOLO11n).
- **Điều khiển Flash LED (`/flash?state=1|0`):** Bật/tắt đèn LED siêu sáng (GPIO 4) để trợ sáng ban đêm hoặc trong thùng rác kín.
- **Hỗ trợ CORS đầy đủ:** Header `Access-Control-Allow-Origin: *` cho phép trình duyệt truy cập trực tiếp hoặc thông qua proxy backend của Flask.

---

## 🛠️ 2. Sơ đồ nối dây Nạp Firmware (ESP32-CAM & USB-to-UART FTDI / CP2102)

Vì ESP32-CAM không tích hợp sẵn chip USB-to-UART, bạn sử dụng mạch nạp USB-to-TTL (FTDI FT232RL hoặc CP2102):

| Chân ESP32-CAM | Chân Mạch nạp FTDI / CP2102 | Ghi chú |
| :--- | :--- | :--- |
| **5V** | **VCC (5V)** | *Nên dùng nguồn 5V/2A ổn định để camera không sụt áp* |
| **GND** | **GND** | Nối chung mass |
| **U0R (GPIO 3)** | **TXD** | Chân nhận dữ liệu |
| **U0T (GPIO 1)** | **RXD** | Chân truyền dữ liệu |
| **IO0 (GPIO 0)** | **GND** | ⚠️ **QUAN TRỌNG: Nối IO0 vào GND khi nạp firmware, tháo ra khi chạy thực tế** |

---

## 🚀 3. Hướng dẫn cài đặt ESP-IDF

### Cách 1: Cài đặt qua VS Code (Khuyên dùng - Nhanh nhất)
1. Cài đặt **Visual Studio Code**.
2. Mở tab **Extensions** (`Ctrl+Shift+X`), tìm kiếm `ESP-IDF` của **Espressif Systems** và bấm **Install**.
3. Nhấn `F1` (hoặc `Ctrl+Shift+P`), gõ `ESP-IDF: Configure ESP-IDF Extension`.
4. Chọn **Express Setup**, chọn phiên bản **ESP-IDF v5.1** hoặc **v5.2**, bấm **Install** và đợi cài đặt hoàn tất.

### Cách 2: Cài đặt bộ công cụ chính thức từ Espressif
1. Chạy file [install_esp_idf.bat](file:///d:/Project_A/trashnet/firmware_esp32_cam/install_esp_idf.bat) trong thư mục này.
2. Hoặc tải bộ cài đặt **ESP-IDF Tools Windows Installer** tại: [docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/windows-setup.html#esp-idf-tools-installer).

---

## ⚙️ 4. Cấu hình Wi-Fi và Biên dịch

1. Mở cửa sổ **ESP-IDF Command Prompt** (hoặc terminal VS Code với môi trường ESP-IDF).
2. Di chuyển vào thư mục `firmware_esp32_cam`:
   ```bash
   cd d:\Project_A\trashnet\firmware_esp32_cam
   ```
3. Cấu hình WiFi SSID và Mật khẩu:
   ```bash
   idf.py menuconfig
   ```
   - Vào menu: **TrashNet ESP32-CAM Configuration**
   - Đổi **WiFi SSID** thành tên WiFi nhà bạn.
   - Đổi **WiFi Password** thành mật khẩu WiFi của bạn.
   - Nhấn `S` để lưu, sau đó nhấn `ESC` để thoát.
   *(Hoặc bạn có thể sửa trực tiếp trong file [main/main.c](file:///d:/Project_A/trashnet/firmware_esp32_cam/main/main.c) tại dòng 27-31).*

4. Biên dịch và nạp code:
   - Hãy chắc chắn chân **IO0 đã nối với GND**.
   - Nhấn nút **Reset** trên ESP32-CAM.
   - Chạy lệnh nạp:
     ```bash
     idf.py -p COM3 flash monitor
     ```
     *(Thay `COM3` bằng cổng COM thực tế của bạn, hoặc chạy file [build_and_flash.bat](file:///d:/Project_A/trashnet/firmware_esp32_cam/build_and_flash.bat))*.

5. Khi nạp xong:
   - **Rút dây nối giữa IO0 và GND ra**.
   - Nhấn nút **Reset** trên ESP32-CAM.
   - Trên màn hình Monitor sẽ in ra:
     ```text
     I (xxxx) trashnet_cam: Da ket noi WiFi! Dia chi IP: 192.168.1.50
     I (xxxx) trashnet_cam: Khoi dong HTTP Server tren cong: 80
     ```

---

## 🌐 5. Kết nối vào Giao diện Web TrashNet

1. Khởi động Web Server TrashNet:
   ```bash
   python app.py
   ```
2. Mở trình duyệt tại: `http://localhost:5001`.
3. Trong giao diện:
   - Chọn tab **ESP32-CAM** ở bảng điều khiển bên trái.
   - Nhập IP của camera (ví dụ: `192.168.1.50` hoặc `http://192.168.1.50`).
   - Nhấn nút **Kết nối**: Video stream trực tiếp từ ESP32-CAM sẽ xuất hiện!
   - Đưa rác trước ống kính camera ESP32 và nhấn **Chụp & Nhận diện ESP32** (hoặc bật **Tự động quét Auto-Scan**) để hệ thống phân loại rác thông minh.

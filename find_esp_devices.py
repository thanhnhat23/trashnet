# -*- coding: utf-8 -*-
"""
TrashNet - Tool Quét và Tổng hợp IP Thiết bị ESP32 (ESP32-CAM + ESP32 Actuator)
Tự động dò subnet LAN, nhận diện thiết bị, kiểm tra COM port và đồng bộ vào Web UI.
"""

import sys
import os
import json
import time
import socket
import urllib.request
import urllib.error
import webbrowser
from concurrent.futures import ThreadPoolExecutor

# Đảm bảo in tiếng Việt có dấu chuẩn xác trên Windows Command Prompt / PowerShell
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


SETTINGS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "actuator_settings.json")
FLASK_API_URL = "http://127.0.0.1:5001/api/actuator/settings"

def get_local_ip_and_subnets():
    """Lấy danh sách các dải mạng IPv4 của máy tính"""
    subnets = []
    try:
        # Thử lấy IP kết nối ra ngoài
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.5)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        parts = local_ip.split(".")
        if len(parts) == 4:
            subnets.append(".".join(parts[:3]))
    except Exception:
        pass

    # Luôn thêm dải 172.16.0 nếu chưa có (mạng hiện tại của quán cafe / lab)
    if "172.16.0" not in subnets:
        subnets.append("172.16.0")
    if "192.168.1" not in subnets:
        subnets.append("192.168.1")
    # SoftAP của ESP32-CAM nếu mất mạng
    if "192.168.4" not in subnets:
        subnets.append("192.168.4")

    return subnets

def probe_ip(ip_str):
    """Kiểm tra 1 IP xem có phải ESP32 Actuator hoặc ESP32-CAM không"""
    url_status = f"http://{ip_str}/status"
    req = urllib.request.Request(url_status, headers={"User-Agent": "TrashNetScanner/1.0"})
    try:
        t0 = time.time()
        with urllib.request.urlopen(req, timeout=0.85) as resp:
            elapsed_ms = int((time.time() - t0) * 1000)
            raw = resp.read().decode("utf-8", errors="ignore").strip()
            try:
                data = json.loads(raw)
            except Exception:
                data = {"raw": raw}
            
            # Phân loại thiết bị
            # 1. ESP32-CAM
            if data.get("device") == "ESP32-CAM" or "role" in data or "capture" in raw or "auto_upload" in data:
                return {
                    "type": "CAM",
                    "name": "ESP32-CAM (Camera AI Phân loại)",
                    "ip": ip_str,
                    "url": f"http://{ip_str}",
                    "stream_url": f"http://{ip_str}/stream",
                    "capture_url": f"http://{ip_str}/capture",
                    "latency_ms": elapsed_ms,
                    "details": data
                }
            
            # 2. ESP32 Actuator (Thường)
            if "current_bin" in data or "rotate_time_ms" in data or "rotate_90_ms" in data or "dump_fwd_ms" in data:
                return {
                    "type": "ACTUATOR",
                    "name": "ESP32 Thường (Actuator - Mâm Xoay + Hất Rác + LCD)",
                    "ip": ip_str,
                    "url": f"http://{ip_str}",
                    "status_url": f"http://{ip_str}/status",
                    "calib_url": f"http://{ip_str}/calib",
                    "latency_ms": elapsed_ms,
                    "details": data
                }

            return {
                "type": "UNKNOWN_HTTP",
                "name": f"Thiết bị HTTP ({ip_str})",
                "ip": ip_str,
                "url": f"http://{ip_str}",
                "latency_ms": elapsed_ms,
                "details": data
            }
    except Exception:
        # Nếu /status lỗi, thử kiểm tra endpoint /stream của ESP32-CAM
        try:
            url_root = f"http://{ip_str}/"
            req2 = urllib.request.Request(url_root, headers={"User-Agent": "TrashNetScanner/1.0"})
            with urllib.request.urlopen(req2, timeout=0.6) as resp2:
                raw2 = resp2.read().decode("utf-8", errors="ignore")
                if "ESP32-CAM" in raw2 or "capture" in raw2:
                    return {
                        "type": "CAM",
                        "name": "ESP32-CAM (Camera AI Phân loại)",
                        "ip": ip_str,
                        "url": f"http://{ip_str}",
                        "stream_url": f"http://{ip_str}/stream",
                        "capture_url": f"http://{ip_str}/capture",
                        "latency_ms": 100,
                        "details": {}
                    }
        except Exception:
            pass

    return None

def check_serial_ports():
    """Kiểm tra các cổng COM USB cắm vào máy tính để bắt lỗi BOOT / GPIO0"""
    com_reports = []
    serial_module = None
    try:
        import serial
        import serial.tools.list_ports
        serial_module = serial
    except ImportError:
        # Thử import từ thư viện IDF nếu có
        idf_python_site = r"D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env\Lib\site-packages"
        if os.path.exists(idf_python_site) and idf_python_site not in sys.path:
            sys.path.append(idf_python_site)
            try:
                import serial
                import serial.tools.list_ports
                serial_module = serial
            except ImportError:
                pass

    if not serial_module:
        return com_reports

    try:
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            dev = p.device
            desc = p.description or ""
            # Lọc cổng có khả năng là ESP (CP210x, CH340, FTDI, USB-Serial)
            if any(k in desc.upper() for k in ["CP210", "CH340", "SERIAL", "UART"]):
                # Kiểm tra trạng thái COM
                status_note = "Đang kết nối bình thường (Idle)"
                try:
                    s = serial.Serial(dev, 115200, timeout=0.3)
                    # Đọc thử ban đầu
                    data = s.read(128).decode("utf-8", errors="ignore")
                    
                    # Nếu chưa có dữ liệu, thử nháy chân DTR/RTS để xem phản hồi reset từ ROM
                    if not data:
                        try:
                            s.dtr = False
                            s.rts = True
                            time.sleep(0.08)
                            s.dtr = True
                            s.rts = False
                            time.sleep(0.12)
                            s.rts = False
                            s.dtr = False
                            time.sleep(0.3)
                            data = s.read(256).decode("utf-8", errors="ignore")
                        except Exception:
                            pass

                    s.close()
                    
                    if "waiting for download" in data or "DOWNLOAD_BOOT" in data:
                        status_note = "⚠️ ĐANG KẸT CHẾ ĐỘ NẠP (DOWNLOAD_BOOT) -> HÃY RÚT DÂY GPIO 0 VÀ BẤM RESET!"
                    elif "IP ESP32-CAM" in data:
                        status_note = f"Đang in log Camera: {data.strip()[:60]}"
                    elif "ets Jul 29" in data or "rst:0x" in data:
                        status_note = "Đã khởi động qua ROM bootloader"
                except Exception:
                    # Cổng đang bị khóa bởi tiến trình khác (ví dụ monitor.bat)
                    status_note = "Đang mở bởi Serial Monitor (đang hoạt động)"

                com_reports.append((dev, desc, status_note))

    except Exception:
        pass

    return com_reports

def scan_network():
    """Quét toàn diện các dải mạng nội bộ"""
    subnets = get_local_ip_and_subnets()
    primary_subnet = subnets[0]
    
    print("=" * 74)
    print("         TRASHNET - TỰ ĐỘNG TỔNG HỢP VÀ DÒ TÌM IP ESP32")
    print("=" * 74)
    print(f"[*] Dải mạng chính đang quét: {primary_subnet}.1 -> {primary_subnet}.254")
    print("[*] Tốc độ quét: Siêu tốc đa luồng (70 workers)... Đợi 2-3 giây...")
    print("-" * 74)

    found_devices = []
    all_ips = [f"{primary_subnet}.{i}" for i in range(1, 255)]

    # Thêm cả SoftAP 192.168.4.1 phòng trường hợp ESP-CAM chưa có wifi
    all_ips.append("192.168.4.1")

    with ThreadPoolExecutor(max_workers=70) as executor:
        results = executor.map(probe_ip, all_ips)
        for r in results:
            if r:
                found_devices.append(r)

    return found_devices

def update_settings_file(actuator_ip, cam_ip):
    """Cập nhật IP vào actuator_settings.json và gửi tới Flask Server"""
    settings = {}
    if os.path.exists(SETTINGS_FILE):
        try:
            with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
                settings = json.load(f)
        except Exception:
            settings = {}

    changed = False
    if actuator_ip and settings.get("actuator_ip") != f"http://{actuator_ip}":
        settings["actuator_ip"] = f"http://{actuator_ip}"
        changed = True

    if cam_ip and settings.get("esp32_cam_ip") != f"http://{cam_ip}":
        settings["esp32_cam_ip"] = f"http://{cam_ip}"
        changed = True

    if changed:
        try:
            with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
                json.dump(settings, f, indent=2, ensure_ascii=False)
            print("[V] Đã lưu IP mới vào file 'actuator_settings.json' thành công!")
        except Exception as e:
            print(f"[!] Lỗi khi ghi file cài đặt: {e}")

        # Đồng bộ trực tiếp tới Flask Server nếu đang chạy
        try:
            payload = json.dumps({
                "actuator_ip": settings.get("actuator_ip"),
                "esp32_cam_ip": settings.get("esp32_cam_ip")
            }).encode("utf-8")
            req = urllib.request.Request(
                FLASK_API_URL,
                data=payload,
                headers={"Content-Type": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=1.0) as resp:
                print("[V] Đã đồng bộ trực tiếp tới Flask Server (Port 5001)!")
        except Exception:
            print("[i] Flask server hiện chưa chạy hoặc khác port (sẽ tự nhận khi khởi động).")

def main():
    devices = scan_network()
    com_ports = check_serial_ports()

    actuator_dev = None
    cam_dev = None

    for d in devices:
        if d["type"] == "ACTUATOR":
            actuator_dev = d
        elif d["type"] == "CAM":
            cam_dev = d

    print("\n" + "=" * 74)
    print("                  KẾT QUẢ TỔNG HỢP IP ESP32 TRASHNET")
    print("=" * 74)

    # 1. ESP32 THƯỜNG (ACTUATOR)
    if actuator_dev:
        dt = actuator_dev.get("details", {})
        print(f"\n[1] ⚙️  ESP32 THƯỜNG (Cơ cấu chấp hành - Mâm xoay + Hất rác + LCD):")
        print(f"    - Địa chỉ IP    : {actuator_dev['ip']}")
        print(f"    - URL Điều khiển: {actuator_dev['url']}")
        print(f"    - Endpoint Trạng thái : {actuator_dev['status_url']}")
        print(f"    - Thùng hiện tại: Thùng #{dt.get('current_bin', 1)} ({dt.get('current_angle', 0)} độ)")
        print(f"    - Thời gian 90° : {dt.get('rotate_90_ms', dt.get('rotate_time_ms', 950))} ms")
        print(f"    - Hất rác (Dump): Fwd {dt.get('dump_fwd_ms', 500)}ms | Wait {dt.get('dump_wait_ms', 1500)}ms | Rev {dt.get('dump_rev_ms', 500)}ms")
        print(f"    - Độ trễ Ping   : {actuator_dev['latency_ms']} ms -> [HOẠT ĐỘNG TỐT - ONLINE]")
    else:
        print(f"\n[1] ⚙️  ESP32 THƯỜNG (Actuator): ❌ CHƯA TÌM THẤY TRÊN MẠNG WI-FI")
        print(f"    -> Kiểm tra nguồn điện ESP32 và đảm bảo đã kết nối cùng mạng WiFi.")

    # 2. ESP32-CAM
    if cam_dev:
        print(f"\n[2] 📷  ESP32-CAM (Camera Vision AI):")
        print(f"    - Địa chỉ IP    : {cam_dev['ip']}")
        print(f"    - URL Live Stream: {cam_dev['stream_url']}")
        print(f"    - URL Chụp ảnh  : {cam_dev['capture_url']}")
        print(f"    - Độ trễ Ping   : {cam_dev['latency_ms']} ms -> [HOẠT ĐỘNG TỐT - ONLINE]")
    else:
        print(f"\n[2] 📷  ESP32-CAM (Camera Vision): ❌ CHƯA TÌM THẤY TRÊN MẠNG WI-FI")
        print(f"    -> Nguyên nhân thường gặp nhất:")
        print(f"       1. Dây nối GPIO 0 với GND vẫn đang cắm (ESP bị kẹt ở chế độ Nạp Download Boot).")
        print(f"          => HÃY RÚT DÂY GPIO 0 RA KHỎI GND VÀ BẤM NÚT RESET (RST) TRÊN ESP32-CAM!")
        print(f"       2. ESP32-CAM đang phát WiFi dự phòng riêng: 'TRASHNET_CAM_AP' (IP: 192.168.4.1).")
        print(f"       3. ESP32-CAM chưa được nạp firmware hoặc chưa cắm nguồn đủ 5V (>= 2A).")

    # 3. THÔNG TIN CỔNG COM USB PHÁT HIỆN ĐƯỢC
    if com_ports:
        print(f"\n[-] 🔌 CÁC CỔNG COM USB PHÁT HIỆN TRÊN MÁY TÍNH:")
        for dev, desc, note in com_ports:
            print(f"    * {dev} ({desc}): {note}")

    print("\n" + "=" * 74)

    # Tự động cập nhật nếu phát hiện
    if actuator_dev or cam_dev:
        act_ip = actuator_dev["ip"] if actuator_dev else None
        c_ip = cam_dev["ip"] if cam_dev else None
        update_settings_file(act_ip, c_ip)

    # Menu tương tác
    print("\nLỰA CHỌN THAO TÁC:")
    if cam_dev:
        print("  [1] Mở Live Stream ESP32-CAM trên trình duyệt")
    if actuator_dev:
        print("  [2] Xem trạng thái JSON ESP32 Thường")
    print("  [3] Mở Giao diện Web Dashboard TrashNet (http://localhost:5001)")
    print("  [R] Quét lại mạng (Refresh)")
    print("  [Q] Thoát")

    try:
        choice = input("\nNhập lựa chọn của bạn (Mặc định nhấn Enter để xem Web): ").strip().upper()
        if choice == "1" and cam_dev:
            webbrowser.open(cam_dev["stream_url"])
        elif choice == "2" and actuator_dev:
            webbrowser.open(actuator_dev["status_url"])
        elif choice == "3" or choice == "":
            webbrowser.open("http://localhost:5001")
        elif choice == "R":
            main()
    except (KeyboardInterrupt, EOFError):
        pass

if __name__ == "__main__":
    main()

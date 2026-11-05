@echo off
chcp 65001 >nul
title TrashNet - ESP-IDF Setup Assistant (Ổ D:)
cls
echo =====================================================================
echo       TRASHNET - CÀI ĐẶT ESP-IDF CHO ESP32 VÀO Ổ D:
echo =====================================================================
echo.

where idf.py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [OK] Đã tìm thấy ESP-IDF trên máy tính của bạn!
    echo Phiên bản hiện tại:
    idf.py --version
    echo.
    echo Bạn có thể tiến hành biên dịch ngay bằng: build_and_flash.bat
    pause
    exit /b 0
)

echo [!] Đã tải sẵn bộ cài đặt chính thức của Espressif về ổ D:
echo     Đường dẫn: D:\Espressif_Installer\esp-idf-tools-setup.exe
echo.
echo =====================================================================
echo [HƯỚNG DẪN QUAN TRỌNG KHI CÀI ĐẶT ĐỂ KHÔNG TỐN DUNG LƯỢNG Ổ C]:
echo 1. Trình cài đặt sẽ mở lên trong giây lát.
echo 2. Khi đến bước chọn thư mục cài đặt (Destination Folder):
echo    - Chọn thư mục trên ổ D: D:\Espressif
echo    - Chọn ESP-IDF Tools: D:\Espressif\tools
echo 3. Nhấn Next cho đến khi hoàn thành cài đặt.
echo =====================================================================
echo.

set /p launch="Bạn có muốn mở bộ cài đặt D:\Espressif_Installer\esp-idf-tools-setup.exe ngay bây giờ? (Y/N): "
if /i "%launch%"=="Y" (
    echo Đang khởi chạy bộ cài đặt...
    start "" "D:\Espressif_Installer\esp-idf-tools-setup.exe"
)

echo.
pause

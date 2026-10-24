@echo off
chcp 65001 >nul
title TrashNet - Quet va Tong hop IP ESP32
cls

echo =====================================================================
echo       TRASHNET - QUÉT VÀ TỔNG HỢP IP ESP32-CAM & ESP32 THƯỜNG
echo =====================================================================
echo.

set "PY_CMD=python"

:: Kiem tra xem lenh python co hoat dong khong
python --version >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    if exist "D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe" (
        set "PY_CMD=D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"
    ) else (
        echo [LOI] Khong tim thay Python! Vui long cai dat Python hoac khoi dong lai may.
        pause
        exit /b 1
    )
)

cd /d "%~dp0"
"%PY_CMD%" "%~dp0find_esp_devices.py"

echo.
pause

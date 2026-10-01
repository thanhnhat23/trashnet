@echo off
title TrashNet - ESP32 Actuator (Servo + Stepper + LCD)
cls
echo =====================================================================
echo    TRASHNET - ESP32 ACTUATOR BUILD AND FLASH (ESP-IDF)
echo    (Dieu khien: Servo MG995 + Stepper 28BYJ-48 + LCD 1602A I2C)
echo =====================================================================
echo.

set "IDF_PATH=D:\Espressif"
set "IDF_TOOLS_PATH=D:\Espressif_Tools\Espressif"
set "IDF_PYTHON_ENV_PATH=D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env"
set "OPENOCD_SCRIPTS=D:\Espressif_Tools\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260304\openocd-esp32\share\openocd\scripts"
set "ESP_ROM_ELF_DIR=D:\Espressif_Tools\Espressif\tools\esp-rom-elfs\20241011\"
set "IDF_CCACHE_ENABLE=1"

set "PATH=D:\Espressif_Tools\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin;D:\Espressif_Tools\Espressif\tools\riscv32-esp-elf-gdb\16.3_20250913\riscv32-esp-elf-gdb\bin;D:\Espressif_Tools\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;D:\Espressif_Tools\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;D:\Espressif_Tools\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin;D:\Espressif_Tools\Espressif\tools\cmake\3.30.2\bin;D:\Espressif_Tools\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260304\openocd-esp32\bin;D:\Espressif_Tools\Espressif\tools\ninja\1.12.1;D:\Espressif_Tools\Espressif\tools\idf-exe\1.0.3;D:\Espressif_Tools\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;D:\Espressif_Tools\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64;D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env\Scripts;D:\Espressif_Tools\Espressif\tools\idf-git\2.44.0\cmd;D:\Espressif\tools;%PATH%"

set "PYTHON=D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"
set "IDF_PY=D:\Espressif\tools\idf.py"

cd /d "%~dp0"

echo [1/3] Dang bien dich firmware Actuator...
call "%PYTHON%" "%IDF_PY%" build
if %ERRORLEVEL% NEQ 0 goto :BUILD_FAIL

echo.
echo =====================================================================
echo CAC CONG COM DANG KET NOI TREN MAY TINH:
powershell -NoProfile -Command "Get-CimInstance Win32_PnPEntity | Where-Object { $_.Caption -match '\(COM\d+\)' } | ForEach-Object { Write-Host '  -> ' $_.Caption }"
echo =====================================================================
echo.
set /p com_port="Nhap cong COM cua ESP32 Thuong (Vi du: COM5 hoac COM6): "
if "%com_port%"=="" set com_port=COM5

echo.
echo [2/3] Dang nap firmware vao %com_port%...
echo (Luu y: Neu man hinh bao 'Connecting...', hay an giu nut BOOT tren ESP32 1 giay)
call "%PYTHON%" "%IDF_PY%" -p %com_port% flash
if %ERRORLEVEL% NEQ 0 goto :FLASH_FAIL

echo.
echo =====================================================================
echo [HOAN TAT] Nap thanh cong Firmware Actuator vao %com_port%!
echo =====================================================================
echo.
set /p open_mon="Mo Serial Monitor de xem IP va test phim 1-4? (Y/N): "
if /i "%open_mon%"=="Y" (
    call "%PYTHON%" "%IDF_PY%" -p %com_port% monitor
)
goto :EOF

:BUILD_FAIL
echo.
echo [LOI] Bien dich that bai! Kiem tra lai ma nguon.
pause
exit /b 1

:FLASH_FAIL
echo.
echo [LOI] Nap that bai! Kiem tra day cap va cong COM.
pause
exit /b 1

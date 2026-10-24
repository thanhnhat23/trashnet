@echo off
title TrashNet - ESP32 Actuator Serial Monitor
cls
echo =====================================================================
echo    TRASHNET - ESP32 ACTUATOR SERIAL MONITOR
echo    (Xem log IP, Test phim 1-4, Can chinh ms Servo 360 va SG90)
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

echo CAC CONG COM DANG KET NOI TREN MAY TINH:
powershell -NoProfile -Command "Get-CimInstance Win32_PnPEntity | Where-Object { $_.Caption -match '\(COM\d+\)' } | ForEach-Object { Write-Host '  -> ' $_.Caption }"
echo =====================================================================
echo.
set /p com_port="Nhap cong COM (Mac dinh COM7, an Enter de dung COM7): "
if "%com_port%"=="" set com_port=COM7

echo.
echo Dang mo Serial Monitor tren %com_port%...
echo (Luu y: De thoat Serial Monitor bat cu luc nao, an to hop phim: [Ctrl] + [ ] ] )
echo =====================================================================
echo.
call "%PYTHON%" "%IDF_PY%" -p %com_port% monitor

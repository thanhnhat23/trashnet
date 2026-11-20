@echo off
rem Build + flash one board. Usage: flash.bat cam|actuator|ac [COMx|x] [--no-monitor]
setlocal enabledelayedexpansion

set "ROLE=%~1"
if /i "!ROLE!"=="ac" set "ROLE=actuator"
if /i "!ROLE!"=="act" set "ROLE=actuator"

if "!ROLE!"=="" set /p ROLE="Board (cam/actuator): "
if /i "!ROLE!"=="ac" set "ROLE=actuator"
if /i "!ROLE!"=="act" set "ROLE=actuator"

if /i not "!ROLE!"=="cam" if /i not "!ROLE!"=="actuator" (
  echo [ERROR] Role must be cam, actuator, or ac
  exit /b 1
)

call "%~dp0idf_env.bat"
if not exist "%PYTHON%" (
  echo [ERROR] ESP-IDF python not found: "%PYTHON%" - check firmware\idf_env.bat
  exit /b 1
)

cd /d "%~dp0!ROLE!"
if not exist sdkconfig call "%PYTHON%" "%IDF_PY%" set-target esp32
echo [1/3] Building !ROLE!...
call "%PYTHON%" "%IDF_PY%" build || (
  echo [ERROR] Build failed
  exit /b 1
)

set "COM=%~2"
if "!COM!"=="" (
  echo CAM: tie GPIO0 to GND then press RESET before flashing. Actuator: hold BOOT if it says Connecting...
  set /p COM="COM port (e.g. COM6 or 6): "
)

if not "!COM!"=="" (
  if /i "!COM:~0,3!" neq "COM" set "COM=COM!COM!"
)

echo [2/3] Flashing !ROLE! to !COM!...
call "%PYTHON%" "%IDF_PY%" -p !COM! flash || (
  echo [ERROR] Flash failed
  exit /b 1
)

echo [3/3] Done. CAM: remove GPIO0-GND link and press RESET. The board joins the SoftAP "TrashNet-..." on first boot.

set "ARG3=%~3"
if /i "!ARG3!"=="--no-monitor" exit /b 0
if /i "!ARG3!"=="-n" exit /b 0
if /i "!ARG3!"=="--skip-monitor" exit /b 0
if "!NON_INTERACTIVE!"=="1" exit /b 0

if /i "!ARG3!"=="--monitor" goto :do_monitor
if /i "!ARG3!"=="-m" goto :do_monitor

set /p MON="Open serial monitor now? (Y/N): "
if /i "!MON!"=="Y" (
:do_monitor
  call "%PYTHON%" "%IDF_PY%" -p !COM! monitor
)

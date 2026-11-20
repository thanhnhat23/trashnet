@echo off
rem Serial monitor. Usage: monitor.bat cam|actuator|ac [COMx|x]   (exit: Ctrl+])
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

set "COM=%~2"
if "!COM!"=="" set /p COM="COM port (e.g. COM6 or 6): "
if not "!COM!"=="" (
  if /i "!COM:~0,3!" neq "COM" set "COM=COM!COM!"
)

echo Starting monitor for !ROLE! on !COM!... (Exit: Ctrl+])
call "%PYTHON%" "%IDF_PY%" -p !COM! monitor

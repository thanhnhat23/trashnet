@echo off
rem Simulated CAM + actuator for trying the UI without hardware
cd /d "%~dp0"
python scripts\fake_devices.py

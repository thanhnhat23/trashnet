@echo off
title TrashNet - Huan luyen Mo hinh AI (ResNet-18 / YOLO11n)
cd /d "%~dp0"
cls
echo ===================================================
echo     TRASHNET - HUAN LUYEN MO HINH AI (GPU RTX 3050)
echo ===================================================
echo.
echo [1] Huan luyen ResNet-18 (train_resnet_4class.py)
echo [2] Huan luyen YOLO11n-cls (train_yolo_4class.py)
echo [3] Huan luyen CA 2 mo hinh lien tiep
echo [4] Thoat
echo.
set /p opt="Chon thao tac [1-4] (Mac dinh 1): "
if "%opt%"=="" set opt=1

if "%opt%"=="1" (
    echo.
    echo Dang huan luyen ResNet-18...
    python train_resnet_4class.py
    pause
    exit /b 0
)

if "%opt%"=="2" (
    echo.
    echo Dang huan luyen YOLO11n-cls...
    python train_yolo_4class.py
    pause
    exit /b 0
)

if "%opt%"=="3" (
    echo.
    echo [1/2] Dang huan luyen ResNet-18...
    python train_resnet_4class.py
    echo.
    echo [2/2] Dang huan luyen YOLO11n-cls...
    python train_yolo_4class.py
    pause
    exit /b 0
)

exit /b 0


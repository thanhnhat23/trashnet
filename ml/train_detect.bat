@echo off
rem Detection pipeline. Usage: ml\train_detect.bat "D:\path\to\team_bbox_dataset" ["D:\path\to\empty_tray_photos"]
setlocal
if "%~1"=="" (echo Usage: train_detect.bat SRC_DATASET [EMPTY_TRAY_DIR] & exit /b 1)
pip install -r ml\requirements.txt || exit /b 1
if "%~2"=="" (python ml\prepare_detect.py --src "%~1") else (python ml\prepare_detect.py --src "%~1" --empty-dir "%~2")
if errorlevel 1 exit /b 1
python ml\train_detect.py --device 0 || exit /b 1
python ml\export_detect.py || exit /b 1
echo Done. server\models\trashnet-det.pt and mobile\assets\models\trashnet-det.tflite

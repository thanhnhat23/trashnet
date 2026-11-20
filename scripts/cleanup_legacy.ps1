# Move legacy files into _legacy\ (nothing is deleted). Run from the project root:
#   powershell -ExecutionPolicy Bypass -File scripts\cleanup_legacy.ps1
# Review _legacy\ afterwards and delete it when you are happy with the new layout.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$dest = Join-Path $root "_legacy"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$items = @(
    "app.py", "templates", "static", "fusion", "predict_resnet.py", "soft_voting.py",
    "weight_search.py", "evaluate_fusion.py", "test_fusion.py", "train_resnet_4class.py",
    "train_yolo_4class.py", "train_models.bat", "tim_ip_esp.bat", "find_esp_devices.py",
    "yolo", "test_yolo", "yolo_detect", "wifi_config.h", "requirements.txt",
    "classification_history.json", "actuator_settings.json", "__pycache__",
    "data\train.py", "data\predict.py", "data\constants.py", "data\resize.py",
    "data\split_data.py", "data\trashnet_resnet18_best.pth", "data\dataset-resized.zip",
    "data\dataset-original", "data\__pycache__", "data\classes.json",
    "runs\classify\train", "runs\classify\train-2",
    "firmware_esp32_actuator", "firmware_esp32_cam",
    "yolo11n-cls.pt", "weights", "models"
)

foreach ($item in $items) {
    $src = Join-Path $root $item
    if (-not (Test-Path $src)) { continue }
    $target = Join-Path $dest $item
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Move-Item -Force -Path $src -Destination $target
    Write-Host "moved  $item"
}
Write-Host "`nDone. Legacy files are in $dest"

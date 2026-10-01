# PowerShell Script to Build and Flash ESP32 Firmware
$ErrorActionPreference = "Stop"

Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host "      TRASHNET - BIEN DICH VA NAP FIRMWARE ESP32 (ESP-IDF)           " -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host ""

$env:IDF_PATH = "D:\Espressif"
$env:IDF_TOOLS_PATH = "D:\Espressif_Tools\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env"
$env:OPENOCD_SCRIPTS = "D:\Espressif_Tools\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260304\openocd-esp32\share\openocd\scripts"
$env:ESP_ROM_ELF_DIR = "D:\Espressif_Tools\Espressif\tools\esp-rom-elfs\20241011\"
$env:IDF_CCACHE_ENABLE = "1"

$toolsPath = "D:\Espressif_Tools\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin;D:\Espressif_Tools\Espressif\tools\riscv32-esp-elf-gdb\16.3_20250913\riscv32-esp-elf-gdb\bin;D:\Espressif_Tools\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;D:\Espressif_Tools\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;D:\Espressif_Tools\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin;D:\Espressif_Tools\Espressif\tools\cmake\3.30.2\bin;D:\Espressif_Tools\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260304\openocd-esp32\bin;D:\Espressif_Tools\Espressif\tools\ninja\1.12.1;D:\Espressif_Tools\Espressif\tools\idf-exe\1.0.3;D:\Espressif_Tools\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;D:\Espressif_Tools\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64;D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env\Scripts;D:\Espressif\tools"
$env:PATH = "$toolsPath;$env:PATH"

$python = "D:\Espressif_Tools\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"
$idfPy = "D:\Espressif\tools\idf.py"

Set-Location $PSScriptRoot

Write-Host "[1/3] Kiem tra va bien dich firmware..." -ForegroundColor Yellow
& $python $idfPy build

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[LOI] Bien dich that bai! Kiem tra lai log o tren." -ForegroundColor Red
    exit 1
}

Write-Host "`n=====================================================================" -ForegroundColor Green
Write-Host "[LUU Y KHI NAP ESP32]:" -ForegroundColor Green
Write-Host "- ESP32-CAM: Noi GPIO 0 voi GND roi bam Reset truoc khi nap."
Write-Host "- ESP32 thuong: Co the can giu nut BOOT khi hien Connecting..."
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host ""

$port = Read-Host "Nhap cong COM (Mac dinh: COM6)"
if ([string]::IsNullOrWhiteSpace($port)) {
    $port = "COM6"
}

Write-Host "`n[2/2] Dang nap firmware vao $port (Xoa sach va ghi moi 100%)..." -ForegroundColor Yellow
& $python $idfPy -p $port erase-flash flash

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[LOI] Nap firmware that bai! Kiem tra cong COM va ket noi." -ForegroundColor Red
    exit 1
}

Write-Host "`n=====================================================================" -ForegroundColor Cyan
Write-Host "[HOAN TAT] Nap thanh cong!" -ForegroundColor Cyan
Write-Host "- ESP32-CAM: Rut day GPIO 0 voi GND ra roi bam nut Reset!"
Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host ""

$mon = Read-Host "Mo Serial Monitor de theo doi servo ngay? (Y/N, mac dinh Y)"
if ([string]::IsNullOrWhiteSpace($mon) -or $mon -eq "Y" -or $mon -eq "y") {
    & $python $idfPy -p $port monitor
}

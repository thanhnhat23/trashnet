# Deletes ONLY the approved items. Permanent (no Recycle Bin). Always prints what it does.
# Run:  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\c_drive_clean.ps1        (add -DryRun to only measure)
param([switch]$DryRun)
$ErrorActionPreference = "Continue"
Write-Host "== TrashNet C: cleanup ==" -ForegroundColor Cyan
$u = $env:USERPROFILE; $l = "$u\AppData\Local"
Write-Host "User profile: $u"
$free0 = (Get-PSDrive C).Free
$targets = @(
  "$l\Temp", "C:\Windows\Temp",
  "$l\npm-cache","$l\pnpm-cache","$l\go-build","$l\pip","$l\ccache","$l\D3DSCache","$l\CrashDumps","$l\NuGet\v3-cache",
  "$l\SquirrelTemp","$l\Package Cache","$l\node-compile-cache","$l\main.kts.compiled.cache","$l\clangd","$l\Jedi",
  "$u\.cache","$u\.gradle\caches","$u\.m2\repository",
  "$l\UnrealEngine","C:\KVRT2020_Data"
)
function Size-GB($p) { [math]::Round(((Get-ChildItem -LiteralPath $p -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum)/1GB, 2) }
foreach ($t in $targets) {
  if (-not (Test-Path -LiteralPath $t)) { Write-Host ("skip (not found): {0}" -f $t) -ForegroundColor DarkGray; continue }
  $gb = Size-GB $t
  if ($DryRun) { Write-Host ("{0,8} GB  {1}" -f $gb, $t); continue }
  Write-Host ("deleting {0,6} GB  {1}" -f $gb, $t) -ForegroundColor Yellow
  # delete contents (keeps the folder itself, skips files in use)
  Get-ChildItem -LiteralPath $t -Force -ErrorAction SilentlyContinue | ForEach-Object {
    try { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction Stop } catch { }
  }
  if ($t -in @("$l\UnrealEngine","C:\KVRT2020_Data")) { Remove-Item -LiteralPath $t -Recurse -Force -ErrorAction SilentlyContinue }
}
if (-not $DryRun) {
  try { Clear-RecycleBin -Force -ErrorAction Stop; Write-Host "Recycle Bin emptied" } catch { Write-Host "Recycle Bin: skipped" }
  $freed = [math]::Round(((Get-PSDrive C).Free - $free0)/1GB, 2)
  Write-Host ("Freed about {0} GB. C: free now {1} GB" -f $freed, [math]::Round((Get-PSDrive C).Free/1GB,1)) -ForegroundColor Green
}
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  Write-Host "Not running as Administrator: C:\Windows\Temp and C:\KVRT2020_Data may be skipped." -ForegroundColor Yellow
}

# Measure candidate cleanup folders on C: (read-only). Run: powershell -ExecutionPolicy Bypass -File scripts\c_drive_report.ps1
$u = $env:USERPROFILE; $l = "$u\AppData\Local"
$items = @(
  "$l\Temp","C:\Windows\Temp",
  "$l\npm-cache","$l\pnpm-cache","$l\go-build","$l\pip","$l\ccache","$l\D3DSCache","$l\CrashDumps","$l\NuGet",
  "$l\SquirrelTemp","$l\Package Cache","$l\node-compile-cache","$l\main.kts.compiled.cache","$l\clangd","$l\Jedi",
  "$u\.cache","$u\.gradle\caches","$u\.m2\repository",
  "$u\VirtualBox VMs","$u\AndroidStudioProjects","$u\.android\avd","$l\Docker","$u\Downloads",
  "C:\Riot Games","C:\XboxGames","C:\Python27","C:\msys64","C:\KVRT2020_Data","$l\Steam","$l\UnrealEngine","$l\Epic Games"
)
$items | ForEach-Object {
  if (Test-Path $_) {
    $s = (Get-ChildItem $_ -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    [pscustomobject]@{ GB = [math]::Round($s/1GB,2); Path = $_ }
  }
} | Sort-Object GB -Descending | Format-Table -AutoSize
$d = Get-PSDrive C; "Free: {0} GB of {1} GB" -f [math]::Round($d.Free/1GB,1), [math]::Round(($d.Used+$d.Free)/1GB,1)

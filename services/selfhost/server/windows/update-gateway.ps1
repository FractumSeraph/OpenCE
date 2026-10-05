# Replaces the native gateway binaries with new ones while the "Halo Web
# Server" task is stopped, then starts it again. Run as administrator:
#
#   powershell -ExecutionPolicy Bypass -File update-gateway.ps1 -From <folder>
#
# <folder> holds halo-native-gateway-win32-x64.exe and/or
# halo-native-gateway-linux-x64 (for example a fresh build's output).
param([Parameter(Mandatory = $true)][string]$From)
$ErrorActionPreference = "Stop"

$taskName = "Halo Web Server"
$bin = Join-Path $PSScriptRoot "..\bin"
$names = "halo-native-gateway-win32-x64.exe", "halo-native-gateway-linux-x64"
$present = $names | Where-Object { Test-Path (Join-Path $From $_) }
if (-not $present) { throw "No gateway binaries in $From" }

$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($task) {
    Stop-ScheduledTask -TaskName $taskName
    # the gateway exits with the server; give Windows a moment to release the file
    for ($i = 0; $i -lt 20 -and (Get-Process halo-native-gateway-win32-x64 -ErrorAction SilentlyContinue); $i++) {
        Start-Sleep -Milliseconds 500
    }
}
foreach ($name in $present) {
    Copy-Item (Join-Path $From $name) (Join-Path $bin $name) -Force
    Write-Host "Updated $name"
}
if ($task) {
    Start-ScheduledTask -TaskName $taskName
    Write-Host "Restarted '$taskName'"
}

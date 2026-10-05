# Starts the Halo server automatically when Windows boots (before anyone logs
# in), as a scheduled task named "Halo Web Server" running as SYSTEM, then
# starts it now. Run as administrator; safe to run again (it replaces the
# task, e.g. after moving this folder).
#
#   powershell -ExecutionPolicy Bypass -File install-autostart.ps1 [-NoStart]
#
# Remove it again with uninstall-autostart.ps1.
param([switch]$NoStart)
$ErrorActionPreference = "Stop"

$taskName = "Halo Web Server"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$launcher = Join-Path $PSScriptRoot "run-service.cmd"
if (-not (Test-Path (Join-Path $root "server\runtime\win32-x64\node.exe"))) {
    throw "The bundled Node.js is missing under $root\server\runtime\win32-x64"
}

$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
}

$action = New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\cmd.exe" `
    -Argument "/d /c `"$launcher`"" -WorkingDirectory $root
$trigger = New-ScheduledTaskTrigger -AtStartup
$principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -MultipleInstances IgnoreNew

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Force `
    -Description "Halo web server from $root (logs in $root\logs)" | Out-Null
Write-Host "Registered '$taskName' to start at boot from $root"

if (-not $NoStart) {
    Start-ScheduledTask -TaskName $taskName
    Write-Host "Started. Log: $root\logs\halo-server.log"
}

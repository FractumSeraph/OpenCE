# Restarts the "Halo Web Server" scheduled task, so that changes to
# config.json or the server's files take effect. Run as administrator:
#
#   powershell -ExecutionPolicy Bypass -File restart.ps1
#
# Stopping the task ends run-service.cmd but can leave its node.exe (and the
# gateway) running and holding the ports, so those are ended too: only the
# ones started from this folder. What happened goes to logs\restart.log.
$ErrorActionPreference = "Continue"
$taskName = "Halo Web Server"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$log = Join-Path $root "logs\restart.log"
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
function Note($text) { $line = "$(Get-Date -Format s) $text"; Write-Host $line; Add-Content -Path $log -Value $line }

$config = Get-Content (Join-Path $root "config.json") -Raw | ConvertFrom-Json
$port = if ($config.http.port) { [int]$config.http.port } else { 8765 }

if (-not (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue)) {
    Note "no '$taskName' task (install it with install-autostart.ps1)"
    exit 1
}
Stop-ScheduledTask -TaskName $taskName
$ours = Get-CimInstance Win32_Process | Where-Object {
    ($_.Name -eq "node.exe" -or $_.Name -like "halo-native-gateway*" -or $_.Name -eq "workerd.exe") -and
    $_.ExecutablePath -and $_.ExecutablePath.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)
}
foreach ($process in $ours) {
    Note "ending $($process.Name) ($($process.ProcessId))"
    Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
}
for ($i = 0; $i -lt 30 -and (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue); $i++) {
    Start-Sleep -Milliseconds 500
}
Start-ScheduledTask -TaskName $taskName
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Seconds 1
    try {
        $response = Invoke-WebRequest -UseBasicParsing -TimeoutSec 3 "http://localhost:$port/"
        if ($response.StatusCode -eq 200) { Note "restarted: http://localhost:$port/ answers"; exit 0 }
    } catch {}
}
Note "the task started, but http://localhost:$port/ did not answer within a minute: see logs\halo-server.log"
exit 1

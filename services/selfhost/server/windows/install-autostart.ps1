# Starts the Halo server automatically when Windows boots (before anyone logs
# in), as a scheduled task named "Halo Web Server", then starts it now. Run
# as administrator once; safe to run again (it replaces the task, e.g. after
# moving this folder).
#
#   powershell -ExecutionPolicy Bypass -File install-autostart.ps1 [-NoStart]
#
# The task runs as the account that installs it (or -User DOMAIN\name), with
# no password stored ("run whether the user is logged on or not", S4U): that
# account can then stop, restart and update the server (restart.ps1,
# update.ps1) without administrator rights, and the server never has more
# rights than it. Such a task cannot reach network shares; everything the
# server uses is in this folder. -AsSystem runs it as SYSTEM instead, as
# earlier versions did, which then needs an administrator for every restart
# and update.
#
# Remove it again with uninstall-autostart.ps1.
param([switch]$NoStart, [switch]$AsSystem, [string]$User = "")
$ErrorActionPreference = "Stop"

$taskName = "Halo Web Server"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$launcher = Join-Path $PSScriptRoot "run-service.cmd"
if (-not (Test-Path (Join-Path $root "server\runtime\win32-x64\node.exe"))) {
    throw "The bundled Node.js is missing under $root\server\runtime\win32-x64"
}
if (-not $User) { $User = [Security.Principal.WindowsIdentity]::GetCurrent().Name }

$existing = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if ($existing) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    # (stopping the task can leave its node.exe and gateway holding the ports)
    Get-CimInstance Win32_Process | Where-Object {
        ($_.Name -eq "node.exe" -or $_.Name -like "halo-native-gateway*" -or $_.Name -eq "workerd.exe") -and
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 2
}

if ($AsSystem) {
    $principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -LogonType ServiceAccount -RunLevel Highest
    $runsAs = "SYSTEM"
} else {
    $principal = New-ScheduledTaskPrincipal -UserId $User -LogonType S4U -RunLevel Limited
    $runsAs = $User
    # the account writes the logs, data and backups, some of which SYSTEM
    # may have made before
    & icacls $root /grant "${User}:(OI)(CI)M" /T /C /Q | Out-Null
}
$action = New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\cmd.exe" `
    -Argument "/d /c `"$launcher`"" -WorkingDirectory $root
$trigger = New-ScheduledTaskTrigger -AtStartup
$settings = New-ScheduledTaskSettingsSet `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable -MultipleInstances IgnoreNew

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Force `
    -Description "Halo web server from $root, as $runsAs (logs in $root\logs)" | Out-Null
Write-Host "Registered '$taskName' to start at boot from $root, as $runsAs"
if (-not $AsSystem) {
    # (a task an administrator registers only administrators may see, start
    # or stop: let the account it runs as do so too)
    $sid = (New-Object Security.Principal.NTAccount $User).Translate([Security.Principal.SecurityIdentifier]).Value
    $scheduler = New-Object -ComObject Schedule.Service
    $scheduler.Connect()
    $scheduler.GetFolder("\").GetTask($taskName).SetSecurityDescriptor("D:(A;;FA;;;BA)(A;;FA;;;SY)(A;;FA;;;$sid)", 0)
    Write-Host "$User may start, stop and change it"
}

if (-not $NoStart) {
    Start-ScheduledTask -TaskName $taskName
    Write-Host "Started. Log: $root\logs\halo-server.log"
}

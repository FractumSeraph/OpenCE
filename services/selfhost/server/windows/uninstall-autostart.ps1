# Stops the Halo server and removes its start-at-boot task. Run as administrator.
$ErrorActionPreference = "Stop"
$taskName = "Halo Web Server"
if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    Write-Host "Removed '$taskName'."
} else {
    Write-Host "'$taskName' is not installed."
}

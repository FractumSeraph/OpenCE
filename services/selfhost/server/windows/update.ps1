# Updates this Halo folder to a newer kit: the game, its server, the lobby
# service, the gateway and Node.js. Run it as the account the server runs as
# (install-autostart.ps1), or as administrator for a server installed with
# -AsSystem:
#
#   powershell -ExecutionPolicy Bypass -File update.ps1
#       the newest kit (halo-server.zip, Windows and Linux) from the fork's
#       web-latest release
#   powershell -ExecutionPolicy Bypass -File update.ps1 -Kit <zip file or URL>
#       another kit (halo-server.zip, or halo-server-windows-x64.zip)
#   ... -ServerOnly
#       keep this folder's game files (a build you tested here) and update
#       only the server
#
# Kept as they are: config.json, data\, logs\, the maps, and any lobby
# artwork you replaced. What it replaces goes to backups\<time>\ (the newest
# three are kept). If the server does not answer after the update, the
# backup is put back. What happened goes to logs\update.log.
param(
    [string]$Kit = "https://github.com/FractumSeraph/OpenCE/releases/download/web-latest/halo-server.zip",
    [switch]$ServerOnly,
    [string]$TaskName = "Halo Web Server",
    [string]$Root = ""
)
$ErrorActionPreference = "Stop"
$taskName = $TaskName
if (-not $Root) { $Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path }

# (this script is in the folder it replaces: run a copy from outside it)
if ($PSCommandPath.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase)) {
    $copy = Join-Path $env:TEMP "halo-update.ps1"
    Copy-Item $PSCommandPath $copy -Force
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $copy, "-Root", $Root, "-Kit", $Kit, "-TaskName", $TaskName)
    if ($ServerOnly) { $arguments += "-ServerOnly" }
    & powershell @arguments
    exit $LASTEXITCODE
}

New-Item -ItemType Directory -Force (Join-Path $Root "logs") | Out-Null
$log = Join-Path $Root "logs\update.log"
function Note($text) { $line = "$(Get-Date -Format s) $text"; Write-Host $line; Add-Content -Path $log -Value $line }

$port = 8765
$configFile = Join-Path $Root "config.json"
if (Test-Path $configFile) {
    $config = Get-Content $configFile -Raw | ConvertFrom-Json
    if ($config.http.port) { $port = [int]$config.http.port }
}
$task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue

# the game's files in public\ (the maps and lobby artwork are not among them)
$gameItems = @("index.html", "halo.html", "halo.js", "halo.wasm", "coi-serviceworker.js",
    "manifest.webmanifest", "assets\pwa", "assets\touch")
$topItems = @("README.md", "HOSTING-VPS.md", "Start Halo (Windows).bat", "start-halo.sh")

function Stop-Halo {
    # (the server stops itself on this file, however it was started: the
    # processes of a task run as an account cannot always be ended from that
    # account's other sessions)
    New-Item -ItemType Directory -Force (Join-Path $Root "data") | Out-Null
    New-Item -ItemType File -Force (Join-Path $Root "data\stop-request") | Out-Null
    for ($i = 0; $i -lt 30 -and (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue); $i++) {
        Start-Sleep -Milliseconds 500
    }
    # (and for the server to end, which ends the task: a task still running
    # ignores the start that follows the update)
    for ($i = 0; $i -lt 40 -and $task -and (Get-ScheduledTask -TaskName $taskName).State -eq "Running"; $i++) {
        Start-Sleep -Milliseconds 500
    }
    if ($task) { Stop-ScheduledTask -TaskName $taskName }
    # (stopping the task can leave its node.exe holding the ports: only ours)
    Get-CimInstance Win32_Process | Where-Object {
        ($_.Name -eq "node.exe" -or $_.Name -like "halo-native-gateway*" -or $_.Name -eq "workerd.exe") -and
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase)
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    for ($i = 0; $i -lt 30 -and (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue); $i++) {
        Start-Sleep -Milliseconds 500
    }
    Start-Sleep -Seconds 1
}

function Start-Halo {
    if (-not $task) { return $true }
    Start-ScheduledTask -TaskName $taskName
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Seconds 1
        # (a start the task ignored, as a task still ending does: again)
        if ($i -in 5, 15, 30 -and (Get-ScheduledTask -TaskName $taskName).State -ne "Running") {
            Start-ScheduledTask -TaskName $taskName
        }
        # (a task that still says it runs the old server, ending slowly, and
        # no answer: stopped, which ends its processes, and started again)
        elseif ($i -in 20, 40 -and (Get-ScheduledTask -TaskName $taskName).State -eq "Running") {
            $answers = $false
            try { $answers = (Invoke-WebRequest -UseBasicParsing -TimeoutSec 3 "http://localhost:$port/").StatusCode -eq 200 } catch {}
            if (-not $answers) {
                Stop-ScheduledTask -TaskName $taskName
                Start-Sleep -Seconds 2
                Start-ScheduledTask -TaskName $taskName
            }
        }
        try {
            if ((Invoke-WebRequest -UseBasicParsing -TimeoutSec 3 "http://localhost:$port/").StatusCode -eq 200) { return $true }
        } catch {}
    }
    return $false
}

# what was moved out, what was moved in, and whether the server is stopped:
# enough to put everything back (Restore-Previous)
$moved = @()
$installed = @()
$stopped = $false
$backup = ""

function Restore-Previous {
    Stop-Halo
    foreach ($item in $script:installed) {
        $path = Join-Path $Root $item
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
    }
    foreach ($item in $script:moved) { Move-Item -LiteralPath (Join-Path $script:backup $item) (Join-Path $Root $item) }
    # (the backup, now empty)
    if ($script:backup -and (Test-Path $script:backup) -and -not (Get-ChildItem $script:backup -Recurse -File)) {
        Remove-Item -Recurse -Force $script:backup
    }
    if (Start-Halo) { Note "back to the previous version" } else { Note "the previous version did not start either: see logs\halo-server.log" }
}

# 1. the kit, unpacked beside the folder (moving it in is then instant)
$work = Join-Path $Root ".update"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory $work | Out-Null
try {
    $zip = $Kit
    if ($Kit -match '^https?://') {
        Note "downloading $Kit"
        $zip = Join-Path $work "kit.zip"
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -UseBasicParsing -Uri $Kit -OutFile $zip
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory((Resolve-Path $zip).Path, (Join-Path $work "kit"))
    $new = Join-Path $work "kit\halo-server"
    if (-not (Test-Path (Join-Path $new "server\server.mjs")) -or
        -not (Test-Path (Join-Path $new "server\runtime\win32-x64\node.exe"))) {
        throw "$Kit is not a Windows Halo kit (no halo-server\server with Node.js for Windows)"
    }

    # 2. stop, and move what is replaced to the backup
    Note "stopping the server"
    Stop-Halo
    $stopped = $true
    $backup = Join-Path $Root ("backups\" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    New-Item -ItemType Directory -Force (Join-Path $backup "public\assets") | Out-Null
    function Move-ToBackup($relative) {
        $from = Join-Path $Root $relative
        if (Test-Path -LiteralPath $from) {
            $to = Join-Path $backup $relative
            New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
            Move-Item -LiteralPath $from $to
            $script:moved += $relative
        }
    }
    Move-ToBackup "server"
    if (-not $ServerOnly) { foreach ($item in $gameItems) { Move-ToBackup "public\$item" } }
    foreach ($item in $topItems) { Move-ToBackup $item }

    # 3. the new kit in
    Move-Item (Join-Path $new "server") (Join-Path $Root "server")
    $installed += "server"
    if (-not $ServerOnly) {
        foreach ($item in $gameItems) {
            $from = Join-Path $new "public\$item"
            if (Test-Path -LiteralPath $from) {
                $to = Join-Path $Root "public\$item"
                New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
                Move-Item -LiteralPath $from $to
                $installed += "public\$item"
            }
        }
        # (lobby artwork only where there is none: yours stays)
        $ui = Join-Path $new "public\assets\ui"
        if (Test-Path $ui) {
            Get-ChildItem $ui -Recurse -File | ForEach-Object {
                $to = Join-Path $Root ("public\assets\ui\" + $_.FullName.Substring($ui.Length + 1))
                if (-not (Test-Path -LiteralPath $to)) {
                    New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
                    Copy-Item -LiteralPath $_.FullName $to
                }
            }
        }
        New-Item -ItemType Directory -Force (Join-Path $Root "public\assets\maps") | Out-Null
        New-Item -ItemType Directory -Force (Join-Path $Root "public\assets\custom_maps") | Out-Null
    }
    foreach ($item in $topItems) {
        $from = Join-Path $new $item
        if (Test-Path -LiteralPath $from) { Move-Item -LiteralPath $from (Join-Path $Root $item); $installed += $item }
    }
    $build = ""
    $index = Join-Path $Root "public\index.html"
    if (Test-Path $index) {
        $match = Select-String -Path $index -Pattern 'halo-build-id" content="([^"]+)' | Select-Object -First 1
        if ($match) { $build = " (game build " + $match.Matches[0].Groups[1].Value + ")" }
    }

    # 4. start it, or put the backup back
    if (Start-Halo) {
        Note "updated from $Kit$build; the replaced files are in $backup"
    } else {
        Note "the server did not answer after the update: putting the backup back"
        Restore-Previous
        exit 1
    }

    # (the newest three backups)
    Get-ChildItem (Join-Path $Root "backups") -Directory | Sort-Object Name -Descending |
        Select-Object -Skip 3 | ForEach-Object { Remove-Item -Recurse -Force $_.FullName }
    if (-not $task) { Note "no '$taskName' task: start the server with 'Start Halo (Windows).bat'" }
} catch {
    Note "update failed: $($_.Exception.Message)"
    if ($stopped) {
        Note "putting the previous version back"
        Restore-Previous
    }
    exit 1
} finally {
    if (Test-Path $work) { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }
}
exit 0

$ErrorActionPreference = "Stop"

# Edit these for quick testing.
# Use "4:3" to simulate the target panel that is detected as 16:9 but is physically 4:3.
$AspectRatio = "16:9"
$BoardSize = 15
$Fullscreen = $true
$MonitorIndex = 1
# Enable retina scaling (high‑DPI) mode.  The application checks for
# GOMOKU_RETINA and will use logical coordinates.
$Retina = $true

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe = Join-Path $ScriptDir "gomoku_gui.exe"
$Src = Join-Path $ScriptDir "AI_Gomoku_GUI.cpp"

if (!(Test-Path $Exe) -or ((Get-Item $Src).LastWriteTime -gt (Get-Item $Exe).LastWriteTime)) {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                [Environment]::GetEnvironmentVariable("Path", "User")
    g++ -std=c++17 -O3 $Src -o $Exe -lgdi32 -luser32
}

$env:GOMOKU_ASPECT = $AspectRatio
$argsList = @("--aspect", $AspectRatio, "--board", "$BoardSize", "--monitor", "$MonitorIndex")
if ($Retina) { $argsList += "--retina" }
if ($Fullscreen) {
    $argsList = @("--fullscreen") + $argsList
}

Start-Process -FilePath $Exe -ArgumentList $argsList -WorkingDirectory $ScriptDir

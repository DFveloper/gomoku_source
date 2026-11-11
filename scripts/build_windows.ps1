param(
    [string]$BuildDir = '.run/build-windows',
    [string]$ToolchainBin = '',
    [int]$Jobs = 2,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $ToolchainBin) {
    $candidates = @(
        "$env:USERPROFILE/Downloads/w64devkit/bin",
        'C:/w64devkit/bin', 'C:/msys64/ucrt64/bin', 'C:/msys64/mingw64/bin'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path "$candidate/g++.exe") { $ToolchainBin = $candidate; break }
    }
}
if ($ToolchainBin) { $env:PATH = "$ToolchainBin;$env:PATH" }
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    throw 'Install w64devkit or provide -ToolchainBin with g++.exe and ninja.exe.'
}
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
& cmake -S $root -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
& cmake --build $BuildDir --parallel $Jobs
if ($LASTEXITCODE) { throw 'Build failed' }
if ($Test) {
    & ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
}
Write-Host "Native apps: $BuildDir/gomoku_sangin.exe and $BuildDir/gomoku_engine_match.exe"

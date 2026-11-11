param([string]$BuildDir = '.run/build-strength', [int]$Pairs = 4)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
$runtime = Join-Path $BuildDir 'third_party/runtime'
& (Join-Path $BuildDir 'strength_check.exe') $runtime
if ($LASTEXITCODE) { throw 'Actual Rapfi + Alpha replies must pass before measuring strength' }
$report = Join-Path $BuildDir 'rapfi-strength-validation.log'
& (Join-Path $BuildDir 'engine_benchmark.exe') 4 3 $Pairs 1 3 | Tee-Object -FilePath $report
if ($LASTEXITCODE) { throw "Not approved: candidate lost or a game was invalid. See $report" }
Write-Host "Selected opening set passed without losses. See $report"
Write-Host 'This is a bounded validation result, not a guarantee for every position. Sangin default is unchanged.'

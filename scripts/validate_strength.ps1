param([string]$BuildDir = '.run/build-strength', [ValidateRange(1,32)][int]$Threads = 6)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
$runtime = Join-Path $BuildDir 'third_party/runtime'
& (Join-Path $BuildDir 'strength_check.exe') $runtime
if ($LASTEXITCODE) { throw 'Actual Rapfi + Alpha replies must pass before measuring strength' }
$report = Join-Path $BuildDir ('star-strength-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $report | Out-Null
$failed = 0
$completed = 0
foreach ($opponent in @(4,5)) {
    foreach ($case in 0..17) {
        $log = Join-Path $report ("opponent-{0}-case-{1:00}.log" -f $opponent,$case)
        & (Join-Path $BuildDir 'engine_benchmark.exe') --star-case $opponent $case $Threads $runtime | Tee-Object -FilePath $log
        if ($LASTEXITCODE) { $failed++ }
        $completed++
    }
}
if ($completed -ne 36 -or $failed -ne 0) {
    throw "Not approved: $failed failed games of $completed. See $report"
}
Write-Host "All 36 games passed without losses. See $report"
Write-Host 'This is a bounded validation result, not a guarantee for every position. Sangin default is unchanged.'

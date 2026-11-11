param([string]$BuildDir = '.run/build-windows', [switch]$Candidate)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
$listener = New-Object System.Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$serverArgs = @('--host','127.0.0.1','--port',"$port")
if ($Candidate) { $serverArgs += '--sangin-candidate' }
$server = Start-Process -FilePath (Join-Path $BuildDir 'gomoku_webui.exe') -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
$base = "http://127.0.0.1:$port"
try {
    $page = $null
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try { $page = Invoke-WebRequest "$base/sangin/" -UseBasicParsing; break } catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $page -or $page.Content -notmatch 'public_app.js') { throw 'Sangin did not load its own client in default local mode' }
    $client = Invoke-WebRequest "$base/public_app.js" -UseBasicParsing
    if ($client.Content -notmatch 'rulesStart') { throw 'Start button handler missing' }
    $capabilities = Invoke-RestMethod "$base/sangin/api/public/capabilities" -Method Post -ContentType application/json -Body '{}'
    if ($capabilities.engines[0].id -ne 4) { throw 'Sangin engine is not Rapfi' }
    $move = Invoke-RestMethod "$base/sangin/api/public/move" -Method Post -ContentType application/json -Body '{"size":15,"moves":[],"r":7,"c":7}' -TimeoutSec 90
    if ($move.aiMove.Count -ne 2 -or ($move.aiMove[0] -eq 7 -and $move.aiMove[1] -eq 7)) { throw 'AI did not return a legal reply to the first move' }
    $reviewBody = @{size=15; moves=@(@(7,7),@($move.aiMove[0],$move.aiMove[1]))} | ConvertTo-Json -Depth 5 -Compress
    $review = Invoke-RestMethod "$base/sangin/api/public/review" -Method Post -ContentType application/json -Body $reviewBody -TimeoutSec 90
    if ($review.reviews.Count -ne 2) { throw 'Two-move review failed' }
    $ordinary = Invoke-RestMethod "$base/api/state"
    if (-not $ordinary.size) { throw 'Ordinary local WebUI is broken' }
    Write-Host 'PASS: default localhost Sangin script, start handler, capabilities, first AI reply, review and ordinary WebUI'
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id }
}

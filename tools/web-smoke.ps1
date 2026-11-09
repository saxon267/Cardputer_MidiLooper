# Run the interactive web preview through Edge headless + CDP.
#   pwsh tools/web-smoke.ps1
param(
    [string]$Edge = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    [int]$Port = 9222
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$url  = "file:///" + ((Join-Path $root "web\index.html") -replace '\\', '/') + "?debug=1"

if (-not (Test-Path $Edge)) { throw "Edge not found at $Edge" }

$profile = Join-Path $env:TEMP ("ml-cdp-" + [guid]::NewGuid().ToString("N"))
$args = @(
    "--headless=new",
    "--disable-gpu",
    "--hide-scrollbars",
    "--no-first-run",
    "--remote-debugging-port=$Port",
    "--remote-debugging-address=127.0.0.1",
    "--user-data-dir=$profile",
    "--window-size=1200,800",
    $url
)

$proc = Start-Process -FilePath $Edge -ArgumentList $args -PassThru
try {
    $ready = $false
    for ($i = 0; $i -lt 40; ++$i) {
        try {
            Invoke-RestMethod "http://127.0.0.1:$Port/json/version" -TimeoutSec 1 | Out-Null
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    if (-not $ready) { throw "CDP did not come up on port $Port" }

    node (Join-Path $root "tools\web-smoke.js")
    exit $LASTEXITCODE
} finally {
    if ($proc -and -not $proc.HasExited) {
        taskkill /PID $proc.Id /T /F 2>&1 | Out-Null
    }
}

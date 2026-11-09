# Regenerate the UI screenshots in docs/ui with headless Edge.
#   pwsh tools/shots.ps1
param(
    [string]$Edge = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $OutDir) { $OutDir = Join-Path $root "docs\ui" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if (-not (Test-Path $Edge)) {
    throw "Edge not found at $Edge (pass -Edge <path>)"
}

$base = "file:///" + ((Join-Path $root "web\index.html") -replace '\\', '/')
$shots = @(
    @("loop-idle.png", "?capture=1"),
    @("loop-rec.png",  "?capture=1&state=rec"),
    @("loop-play.png", "?capture=1&state=play"),
    @("files.png",     "?capture=1&tab=file"),
    @("settings.png",  "?capture=1&settings=1")
)

foreach ($s in $shots) {
    $out = Join-Path $OutDir $s[0]
    & $Edge --headless=new --disable-gpu --hide-scrollbars --no-first-run `
        --user-data-dir="$env:TEMP\ml-edge" --screenshot="$out" --window-size=1040,620 `
        "$base$($s[1])" 2>&1 | Out-Null
    if (-not (Test-Path $out)) { throw "screenshot failed: $out" }
    Write-Host "OK $($s[0])"
}

# Zip a Windows 980 Ti channel-PC tree. No RSI, no cache imagery.
# Run from the repo root after an MSVC build.
param(
    [string]$BuildDir = "build",
    [string]$OutZip = "s376-visual-windows.zip"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Find-Exe([string]$name) {
    $cands = @(
        (Join-Path $BuildDir "Release\$name"),
        (Join-Path $BuildDir $name),
        (Join-Path $BuildDir "RelWithDebInfo\$name")
    )
    foreach ($p in $cands) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

$need = @("s376-visual.exe", "s376-cache.exe", "s376-doctor.exe", "s376-synth.exe")
$found = @()
foreach ($n in $need) {
    $p = Find-Exe $n
    if (-not $p) { throw "missing $n under $BuildDir (build Release first)" }
    $found += $p
}

$stage = Join-Path $env:TEMP ("s376-win-pack-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    New-Item -ItemType Directory -Path (Join-Path $stage "bin") | Out-Null
    foreach ($p in $found) { Copy-Item $p (Join-Path $stage "bin") }
    Copy-Item -Recurse "config" (Join-Path $stage "config")
    Copy-Item -Recurse "data" (Join-Path $stage "data")
    Copy-Item "README.md", "HANDOFF.md" $stage -ErrorAction SilentlyContinue
    if (Test-Path "docs") { Copy-Item -Recurse "docs" (Join-Path $stage "docs") }
    # Refuse Raster / imagery leak
    Get-ChildItem -Recurse $stage -File | ForEach-Object {
        $e = $_.Extension.ToLowerInvariant()
        if ($e -in ".nxt", ".nxb", ".nx3", ".nxs", ".nxo", ".tif", ".tiff", ".jp2") {
            throw "refusing to pack $($_.FullName)"
        }
    }
    if (Test-Path $OutZip) { Remove-Item $OutZip }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $OutZip
    Write-Host "wrote $OutZip"
} finally {
    Remove-Item -Recurse -Force $stage
}

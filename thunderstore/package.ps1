# Packages build/hm64_stardew_hud.nrm into a Thunderstore upload zip.
# Build the mod first (make, then RecompModTool mod.toml build). Run from anywhere: .\thunderstore\package.ps1
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = Split-Path $here -Parent

$manifest = Get-Content (Join-Path $here 'manifest.json') -Raw | ConvertFrom-Json
$modVersion = (Select-String -Path (Join-Path $root 'mod.toml') -Pattern '^version\s*=\s*"([^"]+)"').Matches[0].Groups[1].Value
if ($manifest.version_number -ne $modVersion) {
    throw "Version mismatch: manifest.json has $($manifest.version_number) but mod.toml has $modVersion"
}

$files = @(
    (Join-Path $here 'manifest.json'),
    (Join-Path $here 'README.md'),
    (Join-Path $here 'CHANGELOG.md'),
    (Join-Path $here 'icon.png'),
    (Join-Path $root 'build\hm64_stardew_hud.nrm')
)
foreach ($f in $files) {
    if (-not (Test-Path $f)) { throw "Missing $f" }
}

Add-Type -AssemblyName System.Drawing
$icon = [System.Drawing.Image]::FromFile((Join-Path $here 'icon.png'))
try {
    if ($icon.Width -ne 256 -or $icon.Height -ne 256) { throw "icon.png must be 256x256 (is $($icon.Width)x$($icon.Height))" }
} finally {
    $icon.Dispose()
}

$zip = Join-Path $here "$($manifest.name)-$($manifest.version_number).zip"
Compress-Archive -Path $files -DestinationPath $zip -Force
"Created $zip"

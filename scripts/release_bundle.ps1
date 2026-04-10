# Copy krono_code_vX.Y.Z.{bin,hex,elf} from PlatformIO build dir into release/vX.Y.Z/
# Version is read from the last non-empty line of CHANGELOG.txt (same rule as get_version.py).
# Usage: from repo root, after: platformio run -e blackpill_f411ce
#   powershell -ExecutionPolicy Bypass -File scripts/release_bundle.ps1

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$changelog = Join-Path $repoRoot "CHANGELOG.txt"
if (-not (Test-Path $changelog)) {
    Write-Error "CHANGELOG.txt not found at $changelog"
}

$lines = Get-Content -LiteralPath $changelog | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
$last = $lines[-1]
if ($last -notmatch '^(v\d+\.\d+\.\d+)') {
    Write-Error "Last non-empty CHANGELOG line must start with vX.Y.Z; got: $last"
}
$null = $last -match '^(v\d+\.\d+\.\d+)'
$ver = $Matches[1]
$buildDir = Join-Path $repoRoot ".pio\build\blackpill_f411ce"
$destDir = Join-Path $repoRoot "release\$ver"
New-Item -ItemType Directory -Force -Path $destDir | Out-Null

$base = "krono_code_$ver"
foreach ($ext in @("bin", "hex", "elf")) {
    $src = Join-Path $buildDir "$base.$ext"
    if (-not (Test-Path -LiteralPath $src)) {
        Write-Error "Missing $src - run a successful build first (platformio run -e blackpill_f411ce)."
    }
    Copy-Item -LiteralPath $src -Destination $destDir -Force
    Write-Host "Copied $base.$ext -> $destDir"
}

Write-Host "Release bundle ready: $destDir"

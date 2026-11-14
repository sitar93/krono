# Copy krono_code_vX.Y.Z.{bin,hex,elf} from PlatformIO build dir into release/vX.Y.Z/
# Primary .bin must keep PlatformIO DFU suffix (same as v1.4.0) for jolin.tech updater.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/release_bundle.ps1

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$changelog = Join-Path $repoRoot "CHANGELOG.txt"
if (-not (Test-Path $changelog)) {
    Write-Error "CHANGELOG.txt not found at $changelog"
}

$lines = Get-Content -LiteralPath $changelog | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
$last = $lines[-1]
if ($last -notmatch '^(v\d+\.\d+\.\d+(?:\.\d+)?)') {
    Write-Error "Last non-empty CHANGELOG line must start with vX.Y.Z or vX.Y.Z.W; got: $last"
}
$null = $last -match '^(v\d+\.\d+\.\d+(?:\.\d+)?)'
$ver = $Matches[1]
$buildDir = Join-Path $repoRoot ".pio\build\blackpill_f411ce"
$destDir = Join-Path $repoRoot "release\$ver"
New-Item -ItemType Directory -Force -Path $destDir | Out-Null

function Test-DfuSuffix([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 16) { return $false }
    $i = $bytes.Length - 16
    return ($bytes[$i + 8] -eq 0x55 -and $bytes[$i + 9] -eq 0x46 -and $bytes[$i + 10] -eq 0x44 -and $bytes[$i + 11] -eq 16)
}

$base = "krono_code_$ver"
foreach ($ext in @("bin", "hex", "elf")) {
    $name = "$base.$ext"
    $src = Join-Path $buildDir $name
    if (-not (Test-Path -LiteralPath $src)) {
        Write-Error "Missing $src - run a successful build first."
    }
    if ($ext -eq "bin" -and -not (Test-DfuSuffix $src)) {
        Write-Error "Primary $src must have PlatformIO DFU suffix (UFD) for jolin.tech."
    }
    $len = (Get-Item -LiteralPath $src).Length
    Copy-Item -LiteralPath $src -Destination $destDir -Force
    Write-Host "Copied $name -> $destDir ($len bytes)"
}

$rawName = "${base}_raw.bin"
$rawSrc = Join-Path $buildDir $rawName
if (Test-Path -LiteralPath $rawSrc) {
    Copy-Item -LiteralPath $rawSrc -Destination $destDir -Force
    Write-Host "Copied $rawName -> $destDir (optional Cube-only; not for online updater)"
}

$flashMe = @"
KRONO $ver - flash instructions
================================
ONLINE (https://jolin.tech/krono_updater):
  Use: $base.bin
  (PlatformIO DFU-suffixed - same format as historical releases e.g. v1.4.0)

  Or: $base.hex

Do NOT use *_raw.bin on the online updater.

CubeProgrammer: .hex, .bin, or *_raw.bin are all fine after a chip erase if needed.
"@
Set-Content -LiteralPath (Join-Path $destDir "FLASH_ME.txt") -Value $flashMe -Encoding UTF8

Write-Host "Release bundle ready: $destDir"
Write-Host "Online updater: $base.bin (DFU-suffixed, historical format)."

# DFU upload via PlatformIO (device in STM32 bootloader: BOOT0 held, NRST pulse, release BOOT0).
# Usage from repo root: pwsh scripts/dfu_upload.ps1

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot
$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
if (Test-Path -LiteralPath $pio) {
    & $pio run -e blackpill_f411ce --target upload
} else {
    & platformio run -e blackpill_f411ce --target upload
}

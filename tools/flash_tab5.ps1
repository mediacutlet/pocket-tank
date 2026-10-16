# flash_tab5.ps1 - build the TAB5 image and flash it to the M5Stack Tab5
# (ESP32-P4, 720x1280 MIPI-DSI). Windows / PowerShell: this machine has no
# bash, and the board is found by its COM port rather than a USB serial.
#
#   .\tools\flash_tab5.ps1                      # build + flash the app (the tank)
#   .\tools\flash_tab5.ps1 -SkipBuild           # flash what is in build-tab5
#   .\tools\flash_tab5.ps1 -Full                # a board with M5's factory
#                                               # image: app + model + nvs wiped
#   .\tools\flash_tab5.ps1 -Port COM7 -Monitor
#
# The model (7.56 MB, model/out/model_q4.bin) goes in once, at 0x290000, and is
# skipped after that: the app never rewrites it. The partition table is the
# project's own (firmware/partitions.csv) and it REPLACES whatever the board
# shipped with - hence the one-off nvs wipe in -Full, so the tank starts clean
# instead of reading M5's old keys.
param(
    [string]$Port = 'COM9',
    [switch]$SkipBuild,
    [switch]$Full,
    [switch]$SkipModel,
    [switch]$Monitor
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fw = Join-Path $root 'firmware'
$build = Join-Path $fw 'build-tab5'
# This board builds with ESP-IDF v5.5.x, NOT the v5.4.1 the four Waveshare
# boards use: the M5Stack Tab5 BSP's own dependency on espressif/usb needs a HAL
# v5.4 does not have (usb_dwc_hal_fifo_config_is_valid). Set IDF_TAB5_PATH to
# another checkout if yours lives elsewhere.
$idf = if ($env:IDF_TAB5_PATH) { $env:IDF_TAB5_PATH } else { 'E:\Espressif\frameworks\esp-idf-v5.5.6' }
$env:IDF_TOOLS_PATH = if ($env:IDF_TOOLS_PATH) { $env:IDF_TOOLS_PATH } else { 'E:\Espressif' }

if (-not $SkipBuild) {
    . (Join-Path $idf 'export.ps1') | Out-Null
    Push-Location $fw
    idf.py -B $build -DSDKCONFIG="$build\sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.tab5" build
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "build FAILED" }
    Pop-Location
}
if (-not (Test-Path (Join-Path $build 'pocket_tank.bin'))) { throw "no $build\pocket_tank.bin - build first" }

python -c "import esptool" 2>$null
if ($LASTEXITCODE -ne 0) { throw "esptool is not on this shell's PATH - run . $idf\export.ps1 first" }

# the bootloader + the partition table + the app, at the offsets the build
# itself wrote down (flash_args); the paths inside are relative to the build dir
Push-Location $build
python -m esptool --chip esp32p4 -p $Port -b 460800 write_flash "@flash_args"
$rc = $LASTEXITCODE
Pop-Location
if ($rc -ne 0) { throw "flashing the app failed" }

if ($Full) {
    # M5's image left its own data where our nvs lives: start the tank clean
    python -m esptool --chip esp32p4 -p $Port -b 460800 erase_region 0x9000 0x6000
}
if (-not $SkipModel) {
    if (-not (Test-Path (Join-Path $root 'model\out\model_q4.bin'))) { throw "no model/out/model_q4.bin" }
    # 0x290000: the "model" partition (partitions.csv), 8 MB raw
    python -m esptool --chip esp32p4 -p $Port -b 460800 write_flash 0x290000 (Join-Path $root 'model\out\model_q4.bin')
    if ($LASTEXITCODE -ne 0) { throw "flashing the model failed" }
}
# otadata: an app written to ota_0 alone would be shadowed by a slot the board
# was already booting from (M5's, or a previous run's ota_1)
python -m esptool --chip esp32p4 -p $Port -b 460800 write_flash 0xA90000 (Join-Path $build 'ota_data_initial.bin')

if ($Monitor) {
    . (Join-Path $idf 'export.ps1') | Out-Null
    Push-Location $fw
    idf.py -B $build -p $Port monitor
    Pop-Location
}
Write-Host "flash_tab5: done ($Port)"

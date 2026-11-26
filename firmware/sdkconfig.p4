# the HOME TANK build (Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1, 2026-10-12): on top of sdkconfig.defaults,
# with ESP-IDF 5.5 (the P4's silicon is v3.2: 5.4.1 stops at v1.99) - tools/flash_p4.sh builds it:
#   idf.py -B <dir> -DIDF_TARGET=esp32p4 -DSDKCONFIG=<dir>/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.p4" build
# Every S3-only line of sdkconfig.defaults (octal PSRAM, the S3 cache, 240 MHz) is unknown here and dropped.
CONFIG_IDF_TARGET="esp32p4"
CONFIG_IDF_TARGET_ESP32P4=y
# CONFIG_ESP32P4_SELECTS_REV_LESS_V3 is not set
CONFIG_ESP32P4_REV_MIN_300=y
CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="32MB"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=n
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_400=y
# the bootloader lives at 0x2000 here and the table stays at 0x8000 (nvs never moves): error-level logging keeps it under 24 KB
CONFIG_BOOTLOADER_LOG_LEVEL_ERROR=y
CONFIG_BOOTLOADER_LOG_LEVEL=1
# PSRAM: 32 MB in the package, hex, 200 MHz
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y
CONFIG_IDF_EXPERIMENTAL_FEATURES=y
CONFIG_CACHE_L2_CACHE_256KB=y
CONFIG_CACHE_L2_CACHE_LINE_128B=y
# the P4 world and its ports
CONFIG_POCKET_TANK_P4=y
CONFIG_POCKET_TANK_DISPLAY_SH8601=n
CONFIG_POCKET_TANK_DISPLAY_JD9365=y
CONFIG_POCKET_TANK_TOUCH_FT3168=y
CONFIG_POCKET_TANK_AUDIO_ES8311=y
CONFIG_POCKET_TANK_NO_RADIO=y
CONFIG_POCKET_TANK_CONSOLE_UART=y
# the DSI frame DMA is re-armed from its interrupt at every frame's end: in IRAM, so a
# flash write (the tank's saves) cannot hold it off - a late re-arm is a WHITE frame on the glass
CONFIG_LCD_DSI_ISR_CACHE_SAFE=y
CONFIG_DW_GDMA_CTRL_FUNC_IN_IRAM=y

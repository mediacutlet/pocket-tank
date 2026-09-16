# pocket-tank firmware — ESP32-S3 (Waveshare ESP32-S3-Touch-AMOLED-1.8)
#   and the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (the 4b/ subfolder)
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The 4B's sdkconfig defaults live in 4b/. ESP-IDF auto-discovers
# sdkconfig.defaults.<target> only at the project root, so register the 4B
# file here, for the esp32p4 target only: the S3 build never sees the P4
# symbols (BOARD_4B, hex PSRAM, NO_PIE, the 4B partition table).
if(($ENV{IDF_TARGET} STREQUAL "esp32p4") OR (IDF_TARGET STREQUAL "esp32p4"))
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
        "${CMAKE_CURRENT_LIST_DIR}/4b/sdkconfig.defaults")
else()
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
endif()

project(pocket_tank)

set(SRCS main.c director.c brightness.c batlog.c codec_port.c advisor_llm_esp.c
         update_mode.c model_trailer.c)
# the battery port: the Waveshare boards' AXP2101 PMIC gauge, or the Tab5's
# INA226 input monitor (the board has no battery: the meter stays hidden and the
# port reports the supply instead)
if(CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND SRCS battery_port_tab5.c)
else()
    list(APPEND SRCS battery_port_axp2101.c)
endif()
# the IMU port: the Waveshare boards' QMI8658, or the Tab5's Bosch BMI270 (which
# needs its configuration blob downloaded, hence the espressif/bmi270 component)
if(CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND SRCS imu_port_bmi270.c)
else()
    list(APPEND SRCS imu_port_qmi8658.c)
endif()
# the net port: the ESP32-S3 boards have Wi-Fi, the ESP32-P4 (the M5Stack Tab5)
# has none - its Wi-Fi 6 comes from an ESP32-C6 on SDIO, which this port does
# not bring up yet, so that target gets net_port_stub.c (every network call
# answers "no radio"; see the file's header).
if(CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND SRCS net_port_stub.c)
else()
    list(APPEND SRCS net_port_esp.c)
endif()
if(CONFIG_POCKET_TANK_DISPLAY_SH8601)
    list(APPEND SRCS display_port_sh8601.c)
elseif(CONFIG_POCKET_TANK_DISPLAY_TAB5)
    list(APPEND SRCS display_port_tab5.c)
else()
    list(APPEND SRCS display_port_stub.c)
endif()
if(CONFIG_POCKET_TANK_TOUCH_FT3168 OR CONFIG_POCKET_TANK_TOUCH_GT911)
    list(APPEND SRCS touch_port_ft3168.c)
else()
    list(APPEND SRCS touch_port_stub.c)
endif()
if(CONFIG_POCKET_TANK_AUDIO_ES8311)
    list(APPEND SRCS audio_port_es8311.c)
elseif(CONFIG_POCKET_TANK_AUDIO_TAB5)
    list(APPEND SRCS audio_port_tab5.c)
else()
    list(APPEND SRCS audio_port_stub.c)
endif()
# the radio's components: only where the chip has a radio (the S3 boards). The
# P4 keeps them out of the build entirely.
set(NET_REQUIRES "")
if(NOT CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND NET_REQUIRES esp_wifi esp_netif esp_event esp_http_client esp_https_ota)
endif()
# the Tab5's display port drives the pixel-processing accelerator. Its include
# directory is added by hand below (after the component is registered): in this
# IDF (v5.4.2) esp_driver_ppa does not hand its public include down through a
# REQUIRES entry - the component itself builds and links fine, and
# esp_driver_ledc's include does come through, so it is that one component.
set(TAB5_REQUIRES "")
if(CONFIG_POCKET_TANK_DISPLAY_TAB5)
    list(APPEND TAB5_REQUIRES esp_driver_ledc)
endif()
if(CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND TAB5_REQUIRES bmi270)      # the Tab5's accelerometer (imu_port_bmi270.c)
endif()
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES tankcore llm esp_partition spi_flash esp_timer esp_driver_spi esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_usb_serial_jtag esp_lcd nvs_flash esp_app_format
             app_update json mbedtls bootloader_support ${NET_REQUIRES} ${TAB5_REQUIRES}
    EMBED_FILES "tokenizer.bin" "../../assets/sounds/sounds.bin"
)
# the board marker (net_port_esp.c / net_port_stub.c, version.h PT_BOARD_MARKER_OFFSET):
# nothing references it - the download reads it back from flash - so it is kept
# by name or --gc-sections drops it (ESP-IDF's "custom structure" recipe, app_image_format)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u pt_board_marker")
# the pixel-processing accelerator, by hand (see the note above the register call)
if(CONFIG_POCKET_TANK_DISPLAY_TAB5)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${IDF_PATH}/components/esp_driver_ppa/include")
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::esp_driver_ppa)
endif()

set(SRCS main.c director.c brightness.c batlog.c codec_port.c advisor_llm_esp.c battery_port_axp2101.c imu_port_qmi8658.c
         update_mode.c model_trailer.c link_dev.c)
if(NOT CONFIG_POCKET_TANK_P4)   # THE BLACK BOX and THE I2C BUS GUARD (0.4.3): the pocket boards only. The big screens have never run them (blackbox.h)
    list(APPEND SRCS blackbox.c blackbox_ring.c i2c_guard.c)
endif()
# sd_spike.c (the tank switcher's step 2 test, `sd test`) is left out of 0.3.9.3 (docs/release-0.3.9.3.md D2): it comes
# back with the packages (step 4) - list it here and restore its three hooks in main.c / director.c
if(CONFIG_POCKET_TANK_RADIO_HOSTED)   # the P4 with its C6 (tools/flash_p4.sh --radio): the tank link through the relay; the network to come
    list(APPEND SRCS net_port_stub.c link_port_hosted.c radio_hosted.c radio_up.c radio_mem.c)
elseif(CONFIG_POCKET_TANK_NO_RADIO)   # the P4 without: every network ask refused, the board marker kept
    list(APPEND SRCS net_port_stub.c link_port_stub.c)
else()
    list(APPEND SRCS net_port_esp.c link_spike.c link_port_espnow.c)
endif()
if(CONFIG_POCKET_TANK_DISPLAY_SH8601)
    list(APPEND SRCS display_port_sh8601.c)
elseif(CONFIG_POCKET_TANK_DISPLAY_JD9365)   # the P4's DSI panel (its own target: the component below is built there only)
    list(APPEND SRCS display_port_jd9365.c)
else()
    list(APPEND SRCS display_port_stub.c)
endif()
if(CONFIG_POCKET_TANK_TOUCH_FT3168)
    list(APPEND SRCS touch_port_ft3168.c)
else()
    list(APPEND SRCS touch_port_stub.c)
endif()
if(CONFIG_POCKET_TANK_AUDIO_ES8311)
    list(APPEND SRCS audio_port_es8311.c)
else()
    list(APPEND SRCS audio_port_stub.c)
endif()
set(REQ tankcore llm esp_partition spi_flash esp_timer esp_driver_spi esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_usb_serial_jtag esp_lcd nvs_flash esp_app_format
        esp_wifi esp_netif esp_event esp_http_client esp_https_ota app_update json mbedtls bootloader_support fatfs sdmmc esp_driver_sdspi)
if(IDF_TARGET STREQUAL "esp32p4")   # the home tank: the PPA scales its frame, LEDC dims its backlight, UART0 is its console
    list(APPEND REQ esp_driver_ppa esp_driver_ledc esp_driver_uart esp_mm)
endif()
if("$ENV{PT_P4_RADIO}" STREQUAL "1")   # ... and, with its radio, the hosted stack (firmware/radio: the components come in with this alone)
    list(APPEND REQ pt_radio_deps esp_hosted esp_wifi_remote)
endif()
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${REQ}
    EMBED_FILES "tokenizer.bin" "../../assets/sounds/sounds.bin"
)
# THE APP'S ROOM (0.4.2): the serial console, the battery log, the update page's mode, the Wi-Fi port and the tank
# link's device side never draw a frame or run the model: compiled for size (tankcore's CMakeLists has the shared ones)
set_source_files_properties(director.c batlog.c update_mode.c net_port_esp.c link_dev.c blackbox.c blackbox_ring.c PROPERTIES COMPILE_OPTIONS "-Os")
# the board marker (net_port_esp.c, version.h PT_BOARD_MARKER_OFFSET): nothing
# references it - the download reads it back from flash - so it is kept by name
# or --gc-sections drops it (ESP-IDF's "custom structure" recipe, app_image_format)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u pt_board_marker")
if(CONFIG_POCKET_TANK_RADIO_HOSTED)   # (0.4.3, 11-23) the hosted stack's big DMA buffers in PSRAM, not internal RAM (radio_mem.h)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=eh_host_port_dma_alloc_aligned" "-Wl,--wrap=eh_host_port_dma_alloc")
endif()
# THE BLACK BOX and THE I2C BUS GUARD (0.4.3, blackbox.h / i2c_guard.h): the panic handler and the I2C master driver's
# transaction calls wrapped (ESP-IDF build system, "wrapping functions"): the panic leaves its record, and every
# transaction - ours and esp_lcd's touch drivers' - is counted by chip address and serialized by one lock
# The pocket boards only, as the sources above
if(NOT CONFIG_POCKET_TANK_P4)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler"
        "-Wl,--wrap=i2c_master_transmit" "-Wl,--wrap=i2c_master_multi_buffer_transmit" "-Wl,--wrap=i2c_master_transmit_receive"
        "-Wl,--wrap=i2c_master_receive" "-Wl,--wrap=i2c_master_probe" "-Wl,--wrap=i2c_master_bus_add_device" "-Wl,--wrap=i2c_master_bus_rm_device")
endif()

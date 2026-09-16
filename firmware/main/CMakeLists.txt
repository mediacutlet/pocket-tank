if(CONFIG_POCKET_TANK_BOARD_4B)
    # Waveshare 4B (ESP32-P4): the BSP owns the MIPI-DSI display, the GT911
    # touch and the I2C bus; the board has no PMIC / IMU / external RTC, so
    # the S3 hardware ports are replaced by the stubs below.
    set(SRCS main.c director.c brightness.c batlog.c codec_port.c advisor_llm_esp.c
             p4/display_port_st7703.c p4/touch_port_gt911.c p4/audio_port_es8311.c
             p4/battery_port_stub.c p4/imu_port_stub.c p4/rtc_port_chip.c)
    idf_component_register(
        SRCS ${SRCS}
        INCLUDE_DIRS "." "p4"
        REQUIRES tankcore llm esp32_p4_wifi6_touch_lcd_4b esp_partition nvs_flash esp_timer
                 esp_driver_i2s esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_lcd
        EMBED_FILES "tokenizer.bin" "../../assets/sounds/sounds.bin"
    )
    return()
endif()

set(SRCS main.c director.c brightness.c batlog.c codec_port.c advisor_llm_esp.c s3/battery_port_axp2101.c s3/imu_port_qmi8658.c)
if(CONFIG_POCKET_TANK_DISPLAY_SH8601)
    list(APPEND SRCS s3/display_port_sh8601.c)
else()
    list(APPEND SRCS display_port_stub.c)
endif()
if(CONFIG_POCKET_TANK_TOUCH_FT3168)
    list(APPEND SRCS s3/touch_port_ft3168.c)
else()
    list(APPEND SRCS touch_port_stub.c)
endif()
if(CONFIG_POCKET_TANK_AUDIO_ES8311)
    list(APPEND SRCS s3/audio_port_es8311.c)
else()
    list(APPEND SRCS audio_port_stub.c)
endif()
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "s3"
    REQUIRES tankcore llm esp_partition spi_flash esp_timer esp_driver_spi esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_usb_serial_jtag esp_lcd nvs_flash
    EMBED_FILES "tokenizer.bin" "../../assets/sounds/sounds.bin"
)

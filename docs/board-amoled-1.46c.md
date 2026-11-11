# Waveshare ESP32-S3-Touch-LCD-1.46C

The 1.46C uses a 412 x 412 round TFT with the SPD2010 display/touch controller. Its display and touch resets are on TCA9554 expander outputs; it is not the 1.75C AMOLED pendant.

Hardware details are from the [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.46) and [ESP-IDF example](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.46):

| Function | Connection |
|---|---|
| QSPI clock / data 0-3 / chip select | GPIO 40 / 46, 45, 42, 41 / 21 |
| LCD controller | SPD2010, 412 x 412, QSPI |
| LCD reset / touch reset | TCA9554 EXIO2 / EXIO1 |
| Touch | SPD2010 at I2C 0x53, interrupt GPIO 4 |
| I2C | SDA GPIO 11, SCL GPIO 10 |
| Backlight | PWM GPIO 5 |
| I2S PCM5101 | BCLK GPIO 48, LRCK GPIO 38, data GPIO 47; no MCLK |
| IMU / RTC | QMI8658 / PCF85063 on I2C |

## Build and flash

The board has its own world, simulator, SDK configuration, and image identity:

```sh
make -C sim ROUND146=1 check
cd firmware
idf.py -B build-round146 -DSDKCONFIG=build-round146/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.146c" build
```

To flash by USB serial, set `ROUND146_SERIAL` in `tools/boards.local.sh` or the environment and run `bash tools/flash_round146c.sh`. Use `--build-only` to compile without flashing.

## Hardware validation

This support has not yet been flashed or checked on a physical 1.46C. Verify panel orientation and color order, circular stripe edges, touch orientation and calibration, PWM brightness through sleep/wake, audio output, and battery behavior on the device. SPD2010 flush windows are aligned to four columns. The driver requires ESP-IDF 5.4 or newer.

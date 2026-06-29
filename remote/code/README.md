# StackChan-RemoteControl-ESPNow

This repository is designed to remotely control StackChan servo movements via the `ESPNow` protocol.

## Applicable Devices

| Board | Target profile | Joystick I2C pins | Notes |
|---|---|---|---|
| M5StickC Plus | `M5StickC Plus` | SDA = GPIO0, SCL = GPIO26 | Uses the legacy StickC-family HAT wiring |
| M5StickS3 | `M5StickS3` | SDA = GPIO9, SCL = GPIO10 | Uses M5Unified `M5.Ex_I2C` / Grove; values are printed at boot |

> Note: M5Stack's StickS3 product notes list Hat Mini JoyC (SKU U156) as structurally incompatible with StickS3. Directly attaching MiniJoyC to the StickS3 HAT connector may not expose any I2C device. Use a Grove joystick unit, or a known-good adapter wiring to the Grove/Ex_I2C pins.

## Board Selection

Choose the board profile in `idf.py menuconfig`:

`StackChan remote controller -> Target board`

Then select either:
- `M5StickC Plus`
- `M5StickS3`

The firmware prints the resolved joystick I2C pins during boot so you can confirm the active profile.

## Compilation Environment

* IDF：ESP-IDF v5.5.4 verified (v5.4.x may also work)
* Device Type：esp32 / esp32s3 (match the board you build for)
* M5Unified：0.2.13 or later is required for M5StickS3 board detection

## Compilation and Flashing

1. Select the target chip and board profile:

```sh
idf.py set-target esp32s3
idf.py menuconfig
```

Then choose `StackChan remote controller -> Target board -> M5StickS3`.

2. Build and flash:

```sh
idf.py build
idf.py -p /dev/cu.usbmodem21101 flash
```

On macOS, prefer `/dev/cu.*` over `/dev/tty.*` for flashing when both are present.

## Package Firmware

```
esptool.py --chip esp32 merge_bin -o StackChan-RemoteControl-ESPNow-jyy-20251231_0x0.bin 0x1000 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\StackChan-RemoteControl-ESPNow.bin
```

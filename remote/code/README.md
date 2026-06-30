# StackChan-RemoteControl-ESPNow

This firmware remotely controls StackChan servo movements over `ESPNow`.

## Applicable Device

* M5StickC Plus + Hat Mini JoyC

## Build Environment

* ESP-IDF: v5.5.x
* Target: `esp32`

## Build

```bash
source /Users/p00939/esp-idf-v5.5.4/export.sh
idf.py build
```

## Flash

```bash
idf.py -p /dev/cu.usbserial-7552AB6538 -b 1500000 flash
```

Notes:

1. This project uses the custom partition table in `partitions.csv`.
2. The generated app binary is for `esp32` / M5StickC Plus.
3. Flashing at `1500000` baud is confirmed to work on the current setup.

## Package Firmware

```bash
python -m esptool --chip esp32 merge_bin -o StackChan-RemoteControl-ESPNow-merged.bin \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/StackChan-RemoteControl-ESPNow.bin
```

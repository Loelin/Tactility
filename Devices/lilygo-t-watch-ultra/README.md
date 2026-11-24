# LilyGO T-Watch Ultra

## Build and flash the firmware

```bash
python3 device.py lilygo-t-watch-ultra
idf.py -B build build
idf.py -B build -p /dev/cu.usbmodemXXXX flash
```

User data lives on the SD card, which the watch mounts as `/sdcard`.

## Power button and deep sleep

The **PWR** button is the AXP2101 `PWRKEY`, whose IRQ output is GPIO7. A short press while the
watch is awake enters deep sleep; the same press wakes it again. The sleep entry also powers down
the display, GNSS, LoRa, speaker, sensor and NFC rails to reduce drain. The SD card and ESP32 rails
stay on.

The external PCF85063A RTC is backed by its coin cell and keeps time through deep sleep. On wake,
the existing GPS time-sync service can update the system clock and write it back to the RTC.
**Miscellaneous → GPS Sync on Wake** controls this and is enabled by default. It requires location
services; the board enables them at boot while the switch is on.

Deep sleep current is roughly 1.1 mA with RTC backup on. Full power-off is about 77 µA but requires
a cold boot; the RTC still keeps time either way.

# BHI260AP IMU driver

Tactility driver for the **Bosch BHI260AP** programmable smart sensor used on the
LilyGO T-Watch Ultra.

The BHI260AP is not a register-mapped IMU: it runs Bosch BSX firmware on an internal
MCU and reports data as virtual sensors over the BHY2 FIFO protocol. This module:

- vendors the Bosch **BHY2 Sensor API** (`private/bhi260x/`, BSD-3-Clause),
- uploads the KLIO firmware to the sensor's RAM at start-up,
- enables the accelerometer and gyroscope virtual sensors (100 Hz),
- exposes them through Tactility's `ImuApi`.

## Firmware blob (not committed)

The Bosch firmware is proprietary and **is not part of this repository**.

Fetch it locally before building:

```bash
python3 Drivers/bhi260ap-module/fetch_firmware.py
```

That writes `private/firmware/bosch_bhi260_klio.h`, which is git-ignored. Without it the
module still compiles, but the driver logs a warning and reports `ERROR_NOT_SUPPORTED`
for IMU reads.

Check the Bosch license terms before redistributing the firmware in binaries.

## Devicetree

```dts
i2c0 {
    bhi260ap: imu0 {
        compatible = "bosch,bhi260ap";
        reg = <0x28>;
        mount-matrix = <1 0 0 0 1 0 0 0 1>;
        pin-interrupt = <&gpio0 8 GPIO_FLAG_NONE>;
    };
};
```

The firmware upload benefits from a faster bus: the T-Watch Ultra runs `i2c0` at 400 kHz.

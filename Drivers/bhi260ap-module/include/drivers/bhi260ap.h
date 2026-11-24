// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct Bhi260apConfig {
    /** Device address (0x28 when HSDO is low, 0x29 when high). */
    uint8_t address;
    /**
     * Row-major 3x3 rotation matrix mapping the sensor frame onto the device frame (Linux IIO
     * "mount-matrix" convention: `out[i] = m[i * 3] * x + m[i * 3 + 1] * y + m[i * 3 + 2] * z`).
     * Identity means the sensor frame already matches the device frame.
     */
    int32_t mount_matrix[9];
    /** Optional host interrupt line (currently polled). */
    struct GpioPinSpec pin_interrupt;
};

#ifdef __cplusplus
}
#endif

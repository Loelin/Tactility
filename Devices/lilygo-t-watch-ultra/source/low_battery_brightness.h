// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Starts the board-local low-battery brightness monitor. */
void low_battery_brightness_start(void);

/** Stops the board-local low-battery brightness monitor. */
void low_battery_brightness_stop(void);

#ifdef __cplusplus
}
#endif

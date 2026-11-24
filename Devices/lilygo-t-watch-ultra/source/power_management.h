// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers wake-time GPS sync and deep-sleep power sequencing for the T-Watch Ultra.
 *
 * The PWR button is exposed as a kernel button by the axp2101 module, so core standby and the
 * button-action service own its short press. This callback set only prepares the AXP2101 and the
 * peripheral rails when the core sleep controller enters deep sleep. The external PCF85063A RTC
 * keeps time through deep sleep, and the first GPS time fix after wake updates the system clock
 * and RTC.
 */
void power_management_start(void);

/** Unregisters the deep-sleep and wake-time callbacks. */
void power_management_stop(void);

#ifdef __cplusplus
}
#endif

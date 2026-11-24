// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Devicetree configuration for the AXP2101 PWRKEY button. */
struct Axp2101PwrkeyConfig {
    /** BUTTON_KEY_* code emitted for a short press */
    uint32_t short_press_code;
};

#ifdef __cplusplus
}
#endif

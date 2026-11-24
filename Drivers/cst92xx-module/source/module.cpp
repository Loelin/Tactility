// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver cst92xx_driver;

static Driver* const cst92xx_drivers[] = {
    &cst92xx_driver,
    nullptr
};

Module cst92xx_module = {
    .name = "cst92xx",
    .start = nullptr,
    .stop = nullptr,
    .drivers = cst92xx_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"

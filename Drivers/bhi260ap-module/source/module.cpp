// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver bhi260ap_driver;

static Driver* const bhi260ap_drivers[] = {
    &bhi260ap_driver,
    nullptr
};

Module bhi260ap_module = {
    .name = "bhi260ap",
    .start = nullptr,
    .stop = nullptr,
    .drivers = bhi260ap_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"

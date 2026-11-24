// SPDX-License-Identifier: Apache-2.0
#pragma once

// Compatibility shim for the vendored Bosch BHY2 sources, which include
// "../../SensorBuildOpt.h" (SensorLib's build-options header).
#define SENSORLIB_EXCLUDE_BHI_COMMON 0

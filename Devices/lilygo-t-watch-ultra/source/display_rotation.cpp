// SPDX-License-Identifier: Apache-2.0

// Board-local rotation capability override.
//
// The core's hardware-rotation path changes the panel's MADCTL and expects a physical-orientation
// frame buffer. The panel must keep its fixed native orientation instead, because its flushes are
// full-width strips in the logical frame and the core's software rotation owns the 90/180/270
// mapping. Reporting no hardware swap/mirror support keeps the core out of the MADCTL path.
//
// CAP_SET_GAP is hidden as well. The core's rotation path swaps the gap axes at 90/270 whenever it
// thinks the panel's axes rotate, but the panel's native buffer keeps the column gap fixed: it
// must stay gap-x=22/gap-y=0 at every rotation. Hiding the capability leaves the gap at the values
// the CO5300 driver already applied from the devicetree in start(), instead of the core rewriting
// it to gap-x=0/gap-y=22 and clipping the landscape UI 22 columns to the left.

#include <tactility/device.h>
#include <tactility/drivers/display.h>

#include <cstring>

extern "C" bool __real_display_has_capability(struct Device* device, uint32_t capability);

extern "C" bool __wrap_display_has_capability(struct Device* device, uint32_t capability) {
    if (device != nullptr && device->name != nullptr && std::strcmp(device->name, "display") == 0 &&
        (capability == DISPLAY_CAPABILITY_CAP_SWAP_XY ||
         capability == DISPLAY_CAPABILITY_CAP_MIRROR ||
         capability == DISPLAY_CAPABILITY_CAP_SET_GAP)) {
        return false;
    }

    return __real_display_has_capability(device, capability);
}

# BHI260AP firmware (proprietary, not committed)

Place `bosch_bhi260_klio.h` in this directory. The build enables the firmware path
automatically when the file exists.

Fetch it from SensorLib:

```bash
python3 Drivers/bhi260ap-module/fetch_firmware.py
```

The file is the KLIO firmware variant used by LilyGoLib's T-Watch Ultra bring-up
(`#define BOSCH_BHI260_KLIO`).

This header is intentionally git-ignored: it is Bosch proprietary firmware, not part of
the Tactility source tree. Verify the redistribution terms with Bosch before shipping it
inside a binary.

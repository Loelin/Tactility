#!/usr/bin/env python3
"""Fetch the proprietary Bosch BHI260AP KLIO firmware header.

The header is NOT committed to this repository. It is written to
Drivers/bhi260ap-module/private/firmware/bosch_bhi260_klio.h, which is git-ignored.

Source: LilyGo's SensorLib, the same firmware variant used by LilyGoLib for the
T-Watch Ultra (BOSCH_BHI260_KLIO).
"""

import pathlib
import urllib.request

URL = (
    "https://raw.githubusercontent.com/lewisxhe/SensorLib/master/"
    "src/bosch/firmware/bhi260/bosch_bhi260_klio.h"
)
OUTPUT = pathlib.Path(__file__).resolve().parent / "private" / "firmware" / "bosch_bhi260_klio.h"


def main() -> None:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {URL}")
    with urllib.request.urlopen(URL, timeout=60) as response:
        data = response.read()
    OUTPUT.write_bytes(data)
    print(f"Wrote {OUTPUT} ({len(data)} bytes)")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Upload via PlatformIO, without storing the OTA password in the project."""
import argparse
import getpass
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", nargs="?", default=os.getenv("STAIRS_OTA_HOST", "stairslight.local"))
    parser.add_argument("--pio", help="Path to PlatformIO executable")
    args = parser.parse_args()
    pio = args.pio or shutil.which("pio")
    if not pio:
        installed = Path.home() / ".platformio/penv/bin/pio"
        if installed.is_file():
            pio = str(installed)
    if not pio:
        parser.error("PlatformIO not found; pass --pio /path/to/pio")
    password = os.getenv("STAIRS_OTA_PASSWORD") or getpass.getpass("OTA password: ")
    if not password:
        parser.error("OTA password is required")
    env = dict(os.environ, STAIRS_OTA_HOST=args.host, STAIRS_OTA_PASSWORD=password)
    result = subprocess.run([pio, "run", "-e", "esp32-ota", "-t", "upload"],
                            cwd=Path(__file__).resolve().parent.parent, env=env, check=False)
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()

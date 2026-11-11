"""Flash the compiled sketch over OTA, using config.h for authentication."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys


from compile_firmware import DEFAULT_BUILD

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IP = "192.168.50.83"


def find_uploader(build, explicit=None):
    """Use the platform that produced the image before trying Arduino defaults."""
    if explicit is not None:
        if explicit.is_file():
            return explicit.resolve()
        raise ValueError("Espressif espota.py was not found at --uploader")
    options = build / "build.options.json"
    if options.is_file():
        data = json.loads(options.read_text(encoding="utf-8"))
        for folder in data.get("hardwareFolders", "").split(","):
            candidate = Path(folder) / "tools/espota.py"
            if candidate.is_file():
                return candidate
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    roots = [local / "Arduino15", Path.home() / ".arduino15", Path.home() / "Library/Arduino15"]
    for root in roots:
        versions = root / "packages/esp32/hardware/esp32"
        if not versions.is_dir():
            continue
        candidates = sorted(versions.iterdir(), key=lambda p: tuple(map(int, re.findall(r"\d+", p.name))), reverse=True)
        for version in candidates:
            candidate = version / "tools/espota.py"
            if candidate.is_file():
                return candidate
    raise ValueError("Cannot locate espota.py; pass --uploader PATH")


def check_firmware(firmware):
    if not firmware.is_file():
        raise ValueError("No compiled firmware; run scripts/compile_firmware.py first")
    if firmware.name != ROOT.name + ".ino.bin":
        raise ValueError("Use the sketch application .ino.bin, not a merged image/bootloader")
    sources = [p for p in ROOT.iterdir() if p.suffix in (".h", ".hpp", ".c", ".cpp", ".ino")]
    if (ROOT / "src").is_dir():
        sources += [p for p in (ROOT / "src").rglob("*") if p.suffix in (".h", ".hpp", ".c", ".cpp")]
    if any(p.stat().st_mtime_ns > firmware.stat().st_mtime_ns for p in sources if p.is_file()):
        raise ValueError("Firmware is older than the source/configuration; compile again first")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", default=DEFAULT_IP, help=f"Device IP/hostname (default: {DEFAULT_IP})")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--uploader", type=Path, help="Installed Espressif espota.py (otherwise detected)")
    parser.add_argument("--dry-run", action="store_true", help="Validate paths without contacting the device")
    args = parser.parse_args(argv)
    try:
        build = args.build_dir.resolve()
        firmware = build / (ROOT.name + ".ino.bin")
        check_firmware(firmware)
        uploader = find_uploader(build, args.uploader)
        # Delegate password handling to the existing helper: no credential in
        # the command line or logs, and a nonzero upload result is propagated.
        command = [sys.executable, str(ROOT / "scripts/upload_ota.py"),
                   "--ip", args.ip, "--firmware", str(firmware), "--uploader", str(uploader)]
        print(f"OTA target: {args.ip}; firmware: {firmware}", flush=True)
        if args.dry_run:
            print(f"Uploader: {uploader}")
            return 0
        return subprocess.run(command, cwd=ROOT, check=False).returncode
    except (ValueError, OSError) as error:
        print(f"OTA failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

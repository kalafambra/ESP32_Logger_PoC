"""Compile this sketch with the OTA-capable ESP32 partition layout (stdlib only)."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build-default"
BLE_BUILD = ROOT / "build-ble"
FQBN = "esp32:esp32:esp32:PartitionScheme=default"
BLE_FQBN = "esp32:esp32:esp32:PartitionScheme=no_fs"


def find_cli(explicit=None):
    """Prefer an explicit tool/PATH, then the usual Arduino IDE locations."""
    requested = explicit or os.environ.get("ARDUINO_CLI")
    if requested:
        found = shutil.which(str(requested))
        if found:
            return Path(found)
        raise ValueError("Arduino CLI not found at the requested path")
    found = shutil.which("arduino-cli")
    if found:
        return Path(found)
    candidates = [
        Path("D:/ArduinoIDE/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"),
        Path(os.environ.get("PROGRAMFILES", "C:/Program Files")) / "Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe",
        Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe",
        Path("/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"),
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ValueError("Arduino CLI not found; pass --cli PATH or set ARDUINO_CLI")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", help="Arduino CLI executable (otherwise detected)")
    parser.add_argument("--config", type=Path, help="Arduino CLI YAML configuration")
    parser.add_argument("--ble", action="store_true", help="Enable preserved BLE code with the larger no_fs OTA layout")
    parser.add_argument("--build-dir", type=Path, help="Build directory (default: build-default; --ble: build-ble)")
    parser.add_argument("--clean", action="store_true", help="Discard cached build files")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show detailed compiler commands and build output")
    parser.add_argument("--dry-run", action="store_true", help="Print the command without compiling")
    args = parser.parse_args(argv)
    try:
        cli = find_cli(args.cli)
        config = args.config
        if config is None:
            candidate = Path.home() / ".arduinoIDE/arduino-cli.yaml"
            if candidate.is_file():
                config = candidate
        if config is not None and not config.is_file():
            raise ValueError("Arduino CLI configuration file does not exist")
        if not (ROOT / "config.h").is_file():
            raise ValueError("Create config.h from config.example.h before compiling")
        build = (args.build_dir or (BLE_BUILD if args.ble else DEFAULT_BUILD)).resolve()
        fqbn = BLE_FQBN if args.ble else FQBN
        # --clean lets Arduino delete build contents. Never target the sketch,
        # its parents, or any directory holding firmware sources.
        if build == ROOT or build in ROOT.parents:
            raise ValueError("Build directory must not be the sketch directory or its parent")
        if build.exists() and any(p.suffix in (".ino", ".cpp", ".c") for p in build.iterdir()):
            raise ValueError("Build directory contains source files; choose a separate directory")
        if args.clean and build.exists() and any(build.iterdir()) and not (build / "build.options.json").is_file():
            raise ValueError("Refusing --clean in a nonempty directory without Arduino build metadata")
        command = [str(cli), "compile"]
        if config is not None:
            command += ["--config-file", str(config.resolve())]
        command += ["--fqbn", fqbn, "--build-path", str(build),
                    "--build-property", f"compiler.cpp.extra_flags=-DPICKUP_BLE_ENABLED={int(args.ble)}"]
        if args.clean:
            command.append("--clean")
        if args.verbose:
            command.append("--verbose")
        command.append(str(ROOT))
        print(subprocess.list2cmdline(command), flush=True)
        if args.dry_run:
            return 0
        result = subprocess.run(command, cwd=ROOT, check=False)
        if result.returncode:
            return result.returncode
        firmware = build / (ROOT.name + ".ino.bin")
        if not firmware.is_file():
            raise ValueError("Compiler succeeded but the application image was not found")
        print(f"Firmware: {firmware} ({firmware.stat().st_size} bytes)")
        return 0
    except (ValueError, OSError) as error:
        print(f"Compile failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

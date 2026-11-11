"""Flash an existing ESP32 build over USB serial (COM5 by default)."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from compile_firmware import BLE_FQBN, DEFAULT_BUILD, FQBN, ROOT, find_cli
from flash_ota import check_firmware


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5", help="USB serial port (default: COM5)")
    parser.add_argument("--cli", help="Arduino CLI executable (otherwise detected)")
    parser.add_argument("--config", type=Path, help="Arduino CLI YAML configuration")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("-v", "--verbose", action="store_true", help="Show detailed upload output")
    parser.add_argument("--dry-run", action="store_true", help="Validate paths without opening the port")
    args = parser.parse_args(argv)
    try:
        build = args.build_dir.resolve()
        check_firmware(build / (ROOT.name + ".ino.bin"))
        # USB also writes the bootloader and partition table. Use Arduino's
        # platform upload recipe so addresses and serial reset match the board.
        for suffix in (".ino.bootloader.bin", ".ino.partitions.bin"):
            if not (build / (ROOT.name + suffix)).is_file():
                raise ValueError("Incomplete USB build; run scripts/compile_firmware.py first")
        options = build / "build.options.json"
        fqbn = json.loads(options.read_text(encoding="utf-8")).get("fqbn") if options.is_file() else None
        if fqbn not in (FQBN, BLE_FQBN):
            raise ValueError("Build must use the ESP32 default or no_fs layout; compile again first")
        cli = find_cli(args.cli)
        config = args.config
        if config is None:
            candidate = Path.home() / ".arduinoIDE/arduino-cli.yaml"
            if candidate.is_file():
                config = candidate
        if config is not None and not config.is_file():
            raise ValueError("Arduino CLI configuration file does not exist")
        command = [str(cli), "upload"]
        if config is not None:
            command += ["--config-file", str(config.resolve())]
        command += ["--fqbn", fqbn, "--port", args.port, "--protocol", "serial",
                    "--input-dir", str(build)]
        if args.verbose:
            command.append("--verbose")
        command.append(str(ROOT))
        print(subprocess.list2cmdline(command), flush=True)
        if args.dry_run:
            return 0
        return subprocess.run(command, cwd=ROOT, check=False).returncode
    except (ValueError, OSError) as error:
        print(f"USB upload failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

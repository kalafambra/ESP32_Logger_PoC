"""Offline compiler/uploader orchestration tests; never contacts the board."""
import contextlib
import importlib.util
import io
import json
import os
import sys
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


def load(name):
    file = Path(__file__).resolve().parents[1] / "scripts" / (name + ".py")
    spec = importlib.util.spec_from_file_location(name, file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


compile_firmware = load("compile_firmware")
with patch.dict(sys.modules, {"compile_firmware": compile_firmware}):
    flash_ota = load("flash_ota")
with patch.dict(sys.modules, {"compile_firmware": compile_firmware, "flash_ota": flash_ota}):
    flash_usb = load("flash_usb")


class FirmwareScripts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "ESP32_1"
        self.root.mkdir()
        self.build = self.root / "build-default"
        self.build.mkdir()
        self.config = self.root / "config.h"
        self.config.write_text("// private configuration\n")
        self.image = self.build / "ESP32_1.ino.bin"
        self.image.write_bytes(b"firmware")
        os.utime(self.config, ns=(1_000_000_000, 1_000_000_000))
        os.utime(self.image, ns=(2_000_000_000, 2_000_000_000))
        self.uploader = self.root / "platform/tools/espota.py"
        self.uploader.parent.mkdir(parents=True)
        self.uploader.write_text("# fake uploader\n")
        (self.build / "build.options.json").write_text(json.dumps({"hardwareFolders": str(self.uploader.parent.parent)}))
        for suffix in (".ino.bootloader.bin", ".ino.partitions.bin"):
            (self.build / (self.root.name + suffix)).write_bytes(b"image")
        (self.build / "build.options.json").write_text(json.dumps({
            "hardwareFolders": str(self.uploader.parent.parent), "fqbn": compile_firmware.FQBN}))
        for module in (compile_firmware, flash_ota, flash_usb):
            self.enterContext(patch.object(module, "ROOT", self.root))
        self.enterContext(patch.object(compile_firmware, "find_cli", return_value=Path("arduino-cli")))
        self.enterContext(patch.object(flash_usb, "find_cli", return_value=Path("arduino-cli")))
        self.enterContext(contextlib.redirect_stdout(io.StringIO()))
        self.enterContext(contextlib.redirect_stderr(io.StringIO()))

    def test_compile_uses_project_partition_and_paths(self):
        with patch.object(compile_firmware.subprocess, "run", return_value=SimpleNamespace(returncode=0)) as run:
            self.assertEqual(compile_firmware.main(["--build-dir", str(self.build)]), 0)
        command = run.call_args.args[0]
        self.assertIn("esp32:esp32:esp32:PartitionScheme=default", command)
        self.assertIn("compiler.cpp.extra_flags=-DPICKUP_BLE_ENABLED=0", command)
        self.assertEqual(command[-1], str(self.root))
        self.assertEqual(run.call_args.kwargs["cwd"], self.root)

    def test_ble_compile_selects_larger_layout_and_enables_code(self):
        with patch.object(compile_firmware.subprocess, "run", return_value=SimpleNamespace(returncode=0)) as run:
            self.assertEqual(compile_firmware.main(["--ble", "--build-dir", str(self.build)]), 0)
        command = run.call_args.args[0]
        self.assertIn(compile_firmware.BLE_FQBN, command)
        self.assertIn("compiler.cpp.extra_flags=-DPICKUP_BLE_ENABLED=1", command)

    def test_variants_default_to_separate_build_directories(self):
        for ble, expected in ((False, compile_firmware.DEFAULT_BUILD), (True, compile_firmware.BLE_BUILD)):
            with self.subTest(ble=ble), patch.object(compile_firmware.subprocess, "run") as run, \
                    patch.object(compile_firmware.subprocess, "list2cmdline", return_value="command") as command:
                self.assertEqual(compile_firmware.main(["--dry-run"] + (["--ble"] if ble else [])), 0)
                args = command.call_args.args[0]
                self.assertEqual(args[args.index("--build-path") + 1], str(expected.resolve()))
                run.assert_not_called()

    def test_compile_failure_propagates(self):
        with patch.object(compile_firmware.subprocess, "run", return_value=SimpleNamespace(returncode=7)):
            self.assertEqual(compile_firmware.main(["--build-dir", str(self.build)]), 7)

    def test_compile_dry_run_does_not_start_compiler(self):
        with patch.object(compile_firmware.subprocess, "run") as run:
            self.assertEqual(compile_firmware.main(["--dry-run", "--build-dir", str(self.build)]), 0)
            run.assert_not_called()

    def test_clean_cannot_target_sketch(self):
        with patch.object(compile_firmware.subprocess, "run") as run:
            self.assertEqual(compile_firmware.main(["--clean", "--build-dir", str(self.root)]), 1)
            run.assert_not_called()

    def test_clean_rejects_unrecognized_nonempty_directory(self):
        (self.build / "build.options.json").unlink()
        with patch.object(compile_firmware.subprocess, "run") as run:
            self.assertEqual(compile_firmware.main(["--clean", "--build-dir", str(self.build)]), 1)
            run.assert_not_called()

    def test_uploader_uses_build_platform(self):
        self.assertEqual(flash_ota.find_uploader(self.build), self.uploader)

    def test_stale_configuration_blocks_upload(self):
        os.utime(self.config, ns=(3_000_000_000, 3_000_000_000))
        with patch.object(flash_ota.subprocess, "run") as run:
            self.assertEqual(flash_ota.main(["--build-dir", str(self.build)]), 1)
            run.assert_not_called()

    def test_wrong_image_type_is_rejected(self):
        merged = self.build / "ESP32_1.ino.merged.bin"
        merged.write_bytes(b"merged")
        with self.assertRaisesRegex(ValueError, "application"):
            flash_ota.check_firmware(merged)

    def test_flash_delegates_password_handling_and_propagates_failure(self):
        with patch.object(flash_ota.subprocess, "run", return_value=SimpleNamespace(returncode=4)) as run:
            self.assertEqual(flash_ota.main(["--ip", "device.local", "--build-dir", str(self.build)]), 4)
        command = run.call_args.args[0]
        self.assertIn(str(self.root / "scripts/upload_ota.py"), command)
        self.assertIn("device.local", command)
        self.assertNotIn("--password", command)

    def test_flash_dry_run_never_contacts_device(self):
        with patch.object(flash_ota.subprocess, "run") as run:
            self.assertEqual(flash_ota.main(["--dry-run", "--build-dir", str(self.build)]), 0)
            run.assert_not_called()

    def test_usb_defaults_to_com5_and_serial_upload(self):
        with patch.object(flash_usb.subprocess, "run", return_value=SimpleNamespace(returncode=0)) as run:
            self.assertEqual(flash_usb.main(["--build-dir", str(self.build)]), 0)
        command = run.call_args.args[0]
        self.assertEqual(command[1], "upload")
        self.assertEqual(command[command.index("--port") + 1], "COM5")
        self.assertEqual(command[command.index("--protocol") + 1], "serial")
        self.assertEqual(command[command.index("--input-dir") + 1], str(self.build))

    def test_usb_dry_run_does_not_open_port(self):
        with patch.object(flash_usb.subprocess, "run") as run:
            self.assertEqual(flash_usb.main(["--dry-run", "--port", "COM6", "--build-dir", str(self.build)]), 0)
            run.assert_not_called()

    def test_usb_uses_selected_build_partition_layout(self):
        for fqbn in (compile_firmware.FQBN, compile_firmware.BLE_FQBN):
            with self.subTest(fqbn=fqbn):
                (self.build / "build.options.json").write_text(json.dumps({"fqbn": fqbn}))
                with patch.object(flash_usb.subprocess, "run", return_value=SimpleNamespace(returncode=0)) as run:
                    self.assertEqual(flash_usb.main(["--build-dir", str(self.build)]), 0)
                command = run.call_args.args[0]
                self.assertEqual(command[command.index("--fqbn") + 1], fqbn)

    def test_usb_requires_partition_image(self):
        (self.build / "ESP32_1.ino.partitions.bin").unlink()
        with patch.object(flash_usb.subprocess, "run") as run:
            self.assertEqual(flash_usb.main(["--build-dir", str(self.build)]), 1)
            run.assert_not_called()

    def test_usb_requires_matching_board_layout(self):
        (self.build / "build.options.json").write_text(json.dumps({"fqbn": "esp32:esp32:esp32"}))
        with patch.object(flash_usb.subprocess, "run") as run:
            self.assertEqual(flash_usb.main(["--build-dir", str(self.build)]), 1)
            run.assert_not_called()

    def test_usb_propagates_upload_failure(self):
        with patch.object(flash_usb.subprocess, "run", return_value=SimpleNamespace(returncode=5)):
            self.assertEqual(flash_usb.main(["--verbose", "--build-dir", str(self.build)]), 5)


if __name__ == "__main__":
    unittest.main()

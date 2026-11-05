"""Run external ESP32 continuity/performance tests; never writes firmware settings."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tests"))
from device_suite.runner import main

if __name__ == "__main__":
    raise SystemExit(main())

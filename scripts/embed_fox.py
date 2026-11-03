"""Regenerate the flash-resident PNG header after replacing fox.png."""
from pathlib import Path

root = Path(__file__).resolve().parent.parent
data = (root / "fox.png").read_bytes()
lines = ["#pragma once", "#include <Arduino.h>",
         "// Generated from fox.png; served from flash without a filesystem.",
         "const uint8_t FOX_PNG[] PROGMEM = {"]
lines += [",".join(f"0x{byte:02x}" for byte in data[i:i + 20]) + ","
          for i in range(0, len(data), 20)]
lines.append("};")
(root / "fox_image.h").write_text("\n".join(lines) + "\n", encoding="ascii")
print(f"Embedded fox.png: {len(data)} bytes")

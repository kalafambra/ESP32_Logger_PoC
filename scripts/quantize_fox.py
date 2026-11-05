"""Convert fox.png to a 4-bit, 16-entry indexed palette, then regenerate its C header.

Requires Pillow 13+. No dithering preserves the existing pixel-art edges.
"""
from pathlib import Path
import argparse
import io
import runpy
import subprocess
from PIL import Image

root = Path(__file__).resolve().parent.parent
path = root / "fox.png"
before = path.stat().st_size
parser = argparse.ArgumentParser()
parser.add_argument("--from-git", action="store_true", help="Use the committed fox.png as the input")
args = parser.parse_args()
input_image = io.BytesIO(subprocess.check_output(["git", "show", "HEAD:fox.png"], cwd=root)) if args.from_git else path
with Image.open(input_image) as source:
    original = source.convert("RGBA")
    pixels = list(original.get_flattened_data())
    # Preserve partial-alpha pixels exactly; transparent background must not dominate palette selection.
    special = ([(0, 0, 0, 0)] if any(p[3] == 0 for p in pixels) else [])
    special += sorted({p for p in pixels if 0 < p[3] < 255})
    opaque_count = 16 - len(special)
    if opaque_count < 1:
        raise ValueError("Too many distinct translucent colors to preserve in a 16-color palette")
    opaque = [p[:3] for p in pixels if p[3] == 255]
    palette_source = Image.new("RGB", (len(opaque), 1))
    palette_source.putdata(opaque)
    colors = palette_source.quantize(colors=opaque_count, method=Image.Quantize.MEDIANCUT).getpalette()[:opaque_count*3]
    lookup = Image.new("P", (1, 1))
    lookup.putpalette(colors + colors[:3] * (256-opaque_count))
    indexed = original.convert("RGB").quantize(palette=lookup, dither=Image.Dither.NONE)
    indices = list(indexed.get_flattened_data())
    special_index = {p: opaque_count+i for i, p in enumerate(special)}
    indexed.putdata([(index if index < opaque_count else 0) if p[3] == 255 else
                     special_index[(0, 0, 0, 0) if p[3] == 0 else p]
                     for index, p in zip(indices, pixels)])
    final_colors = colors + [c for p in special for c in p[:3]]
    indexed.putpalette(final_colors + [0] * (768-len(final_colors)))
    indexed.info["transparency"] = bytes([255]*opaque_count + [p[3] for p in special])
    indexed.save(path, format="PNG", bits=4, optimize=True)
with Image.open(path) as check:
    assert check.mode == "P" and check.size == original.size
    assert len(check.getcolors()) <= 16
    alpha = check.convert("RGBA").getchannel("A")
    assert list(original.getchannel("A").get_flattened_data()) == list(alpha.get_flattened_data())
assert path.read_bytes()[24] == 4  # PNG IHDR: four bits per palette index.
print(f"16-color PNG: {before} -> {path.stat().st_size} bytes; {original.size}")
runpy.run_path(str(root / "scripts" / "embed_fox.py"), run_name="__main__")

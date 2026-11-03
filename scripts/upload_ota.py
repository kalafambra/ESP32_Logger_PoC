"""Use the installed Espressif uploader without putting passwords in argv/logs."""
import argparse
import ast
import importlib.util
import logging
from pathlib import Path
import re
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--ip", required=True)
parser.add_argument("--firmware", required=True)
parser.add_argument("--uploader", required=True)
args = parser.parse_args()
config = (Path(__file__).resolve().parent.parent / "config.h").read_text()
match = re.search(r'constexpr\s+char\s+OTA_PASSWORD\[\]\s*=\s*("(?:[^"\\]|\\.)*")\s*;', config)
if not match:
    sys.exit("Cannot find a quoted OTA_PASSWORD in config.h")
try:
    password = ast.literal_eval(match.group(1))
except Exception:
    sys.exit("Cannot decode OTA_PASSWORD; check its string-literal syntax")
if len(password) < 12:
    sys.exit("OTA password must contain at least 12 characters")
if not Path(args.firmware).is_file():
    sys.exit("Compiled firmware not found")
spec = importlib.util.spec_from_file_location("espota", args.uploader)
espota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(espota)
espota.PROGRESS = False
espota.TIMEOUT = 15
logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
sys.exit(espota.serve(args.ip, "0.0.0.0", 3232, 32320, password, False, args.firmware))

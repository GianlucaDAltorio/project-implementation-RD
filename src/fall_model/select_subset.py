"""Copies the FallAllD files used in this project into a smaller folder.

Usage: python select_subset.py <FallAllD_dir> [out_dir] [options]

    out_dir           default data/fallalld/my_subset
    --max-trial N     skip trials numbered above N
    --devices 2,3     loggers to keep (1 neck, 2 wrist, 3 waist)
    --sensors AG      sensor files to keep (the model only uses A and G)
    --activities SET  "default", "all", or a list such as 5,6,101,103

Example (v2 subset):
    python select_subset.py D:/FallAllD data/fallalld/my_subset_v2 --devices 2 --sensors AG --activities all
"""
import argparse
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

FALLS = [101, 103, 107, 111, 112, 113, 115, 117, 119, 127, 129, 130, 131, 132, 133, 134]
NON_FALLS = [11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28] + list(range(29, 41))

ap = argparse.ArgumentParser()
ap.add_argument("src")
ap.add_argument("dst", nargs="?", default=str(ROOT / "data" / "fallalld" / "my_subset"))
ap.add_argument("--max-trial", type=int)
ap.add_argument("--devices", default="2,3")
ap.add_argument("--sensors", default="ABGM")
ap.add_argument("--activities", default="default")
args = ap.parse_args()

src, dst = Path(args.src), Path(args.dst)
devices = {int(x) for x in args.devices.split(",")}
sensors = set(args.sensors.upper())
if args.activities == "default":
    acts = set(FALLS + NON_FALLS)
elif args.activities == "all":
    acts = None
else:
    acts = {int(x) for x in args.activities.split(",")}
if not src.is_dir():
    sys.exit(f"input folder not found: {src.resolve()}")
dst.mkdir(parents=True, exist_ok=True)

pat = re.compile(r"S(\d+)_D(\d)_A(\d+)_T(\d+)_([ABGM])\.dat$")
total = named = copied = 0
for f in sorted(src.rglob("*.dat")):
    total += 1
    m = pat.search(f.name)
    if not m:
        continue
    named += 1
    _, d, a, t, s = m.groups()
    d, a, t = int(d), int(a), int(t)
    if d not in devices or s not in sensors:
        continue
    if acts is not None and a not in acts:
        continue
    if args.max_trial is not None and t > args.max_trial:
        continue
    shutil.copy(f, dst / f.name)
    copied += 1

print(f"{total} .dat files found, {named} with expected names, {copied} copied to {dst}")
if total == 0:
    print("No .dat files under", src.resolve())
elif named == 0:
    print("File names do not match S01_D1_A013_T01_A.dat, e.g.:", next(src.rglob("*.dat")).name)
elif copied == 0:
    print("File names matched but none passed the filters.")

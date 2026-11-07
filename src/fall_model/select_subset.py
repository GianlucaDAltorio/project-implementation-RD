"""Copy the FallAllD files used in this project into a smaller folder.

Keeps the wrist (device 2) and waist (device 3) loggers and the fall and daily-activity
IDs listed below. All four sensor files (_A, _G, _M, _B) of each trial are copied.

Usage:
    python select_subset.py <FallAllD_dir> [out_dir] [max_trial_number]

out_dir defaults to data/fallalld/my_subset.
"""
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

FALLS = [101, 103, 107, 111, 112, 113, 115, 117, 119, 127, 129, 130, 131, 132, 133, 134]
NON_FALLS = [11, 12, 13, 14, 15, 16, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28] + list(range(29, 41))
KEEP_ACT, KEEP_DEV = set(FALLS + NON_FALLS), {2, 3}

src = Path(sys.argv[1])
dst = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "data" / "fallalld" / "my_subset"
cap = int(sys.argv[3]) if len(sys.argv) > 3 else None
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
    _, d, a, t, _ = m.groups()
    d, a, t = int(d), int(a), int(t)
    if d not in KEEP_DEV or a not in KEEP_ACT:
        continue
    if cap is not None and t > cap:
        continue
    shutil.copy(f, dst / f.name)
    copied += 1

print(f"{total} .dat files found, {named} with expected names, {copied} copied to {dst}")
if total == 0:
    print("No .dat files under", src.resolve())
elif named == 0:
    print("File names do not match S01_D1_A013_T01_A.dat, e.g.:", next(src.rglob("*.dat")).name)
elif copied == 0:
    print("File names matched but none passed the device/activity filter.")

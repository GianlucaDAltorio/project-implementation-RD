"""Check that the C detector reproduces the Python model.

Builds harness.c against src/FallDetector_trials and, for each of the 200 windows in
data/features/py_reference.csv, compares the C probability and the 14 features with the
Python values. The C code works in float32, so a feature sitting on a split threshold can
flip one tree; the check allows a probability difference of up to 0.02 (two trees) and
requires the same fall/no-fall decision at the model threshold. Requires gcc and the
FallAllD subset in data/fallalld/my_subset.

Usage:
    python parity_check.py
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[3]
FW = ROOT / "src" / "FallDetector_trials"
SUBSET = ROOT / "data" / "fallalld" / "my_subset"
EXE = Path(__file__).resolve().parent / "harness"

subprocess.run(["gcc", "-O2", "-std=c99", f"-I{FW}", str(Path(__file__).with_name("harness.c")),
                str(FW / "fall_detect.c"), "-lm", "-o", str(EXE)], check=True)

ref = pd.read_csv(ROOT / "data" / "features" / "py_reference.csv")
names = pd.read_csv(ROOT / "data" / "features" / "feature_order.csv").name.tolist()
worst_p = worst_f = 0.0
THR = 0.26
flips = 0
for _, r in ref.iterrows():
    out = subprocess.run([str(EXE), "parity", str(SUBSET), r.file, str(int(r.idx))],
                         capture_output=True, text=True, check=True).stdout.strip().split(",")
    c = np.array(out, dtype=float)
    worst_p = max(worst_p, abs(c[0] - r.p))
    flips += int((c[0] >= THR) != (r.p >= THR))
    worst_f = max(worst_f, float(np.max(np.abs(c[1:] - r[names].to_numpy(dtype=float)))))

print(f"{len(ref)} windows; max probability difference {worst_p:.2e}; max feature difference {worst_f:.2e}; "
      f"decision mismatches {flips}")
sys.exit(0 if worst_p < 0.02 and worst_f < 1e-2 and flips == 0 else 1)

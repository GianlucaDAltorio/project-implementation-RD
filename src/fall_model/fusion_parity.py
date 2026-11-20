"""Checks that the fused model in C gives the same results as Python.

Builds parity_test/harness_fusion.c with the watch's detector code and runs it on the
windows in data/features/fusion_reference.csv: the 29 features and the fall probability
must match, and the decision must be the same. Also checks that the waist board's copy of
fd_features.c is identical to the watch's. Needs gcc and the FallAllD fusion subset.

Usage: python fusion_parity.py [subset_dir]
"""
import filecmp
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "src" / "FallDetector_trials"
WAIST = ROOT / "src" / "WaistBoard"
SUBSET = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data" / "fallalld" / "my_subset_fusion"
HERE = Path(__file__).resolve().parent / "parity_test"
EXE = HERE / "harness_fusion"

if shutil.which("gcc") is None:
    sys.exit("gcc not found on PATH; install it (see README) and open a new terminal")
subprocess.run(["gcc", "-O2", "-std=c99", f"-I{FW}", str(HERE / "harness_fusion.c"), str(FW / "fall_detect.c"),
                str(FW / "fd_features.c"), "-lm", "-o", str(EXE)], check=True)
thr = float(re.search(r"FF_THRESHOLD ([0-9.]+)f", (FW / "fall_model_fusion.h").read_text()).group(1))

ref = pd.read_csv(ROOT / "data" / "features" / "fusion_reference.csv")
names = [c for c in ref.columns if c not in ("trial", "idx", "p")]
worst_p = worst_f = 0.0
flips = 0
for _, r in ref.iterrows():
    out = subprocess.run([str(EXE), str(SUBSET), r.trial, str(int(r.idx))],
                         capture_output=True, text=True, check=True).stdout.strip().split(",")
    c = np.array(out, dtype=float)
    py = r[names].to_numpy(dtype=float)
    both = ~(np.isnan(py) | np.isnan(c[1:]))
    worst_f = max(worst_f, float(np.max(np.abs(c[1:][both] - py[both]))))
    worst_f = max(worst_f, float((np.isnan(py) != np.isnan(c[1:])).any()))
    worst_p = max(worst_p, abs(c[0] - r.p))
    flips += int((c[0] >= thr) != (r.p >= thr))

same = filecmp.cmp(FW / "fd_features.c", WAIST / "fd_features.c", shallow=False) and \
    filecmp.cmp(FW / "fd_features.h", WAIST / "fd_features.h", shallow=False)
print(f"{len(ref)} windows; max probability difference {worst_p:.2e}; max feature difference {worst_f:.2e}; "
      f"decision mismatches {flips}; waist board fd_features identical: {same}")
sys.exit(0 if worst_p < 0.02 and worst_f < 1e-2 and flips == 0 and same else 1)

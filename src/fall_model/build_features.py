"""Whole-trial features for the selected FallAllD subset, with a subject-level split.

Writes one row per trial (peak and spread of acceleration and rotation, jerk, barometer
change) to data/features/features.csv. Subjects are split into training, validation
(7, 10, 13) and test (2, 9, 14) sets by number, so no person appears in two sets and
the split stays the same when the subset changes. The wrist rows of this file supply
the split used by the windowed model. Barometer columns are empty when the subset has
no _B files.

Usage:
    python build_features.py [subset_dir] [ActivityID2Str.m] [out_dir]

Defaults: data/fallalld/my_subset_v2, data/fallalld/ActivityID2Str.m, data/features.
"""
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from scipy.ndimage import median_filter

ROOT = Path(__file__).resolve().parents[2]
FS = 238

src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data" / "fallalld" / "my_subset_v2"
mfile = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "data" / "fallalld" / "ActivityID2Str.m"
out = Path(sys.argv[3]) if len(sys.argv) > 3 else ROOT / "data" / "features"
out.mkdir(parents=True, exist_ok=True)

ACT = {int(n): t.strip() for n, t in
       re.findall(r"case\s+(\d+)\s*\n\s*res=\s*\[res\s+'([^']*)'\]", mfile.read_text())}
DEV = {2: "Wrist", 3: "Waist"}


def rd(p):
    return np.loadtxt(p, delimiter=",", dtype=float)


rows = []
for fa in sorted(src.glob("*_A.dat")):
    m = re.match(r"S(\d+)_D(\d)_A(\d+)_T(\d+)_A\.dat$", fa.name)
    s, d, a, t = map(int, m.groups())
    b = fa.name[:-6]
    acc = rd(fa) * 0.000244
    gyr = rd(fa.with_name(b + "_G.dat")) * 0.07
    fb = fa.with_name(b + "_B.dat")
    bar = rd(fb)[:, 0] if fb.exists() else None
    am = np.linalg.norm(acc, axis=1)
    gm = np.linalg.norm(gyr, axis=1)
    jerk = np.abs(np.diff(am)) * FS
    if bar is not None:
        bs = median_filter(bar, size=5, mode="nearest")
        bar_f = dict(bar_range=bs.max() - bs.min(), bar_net=np.median(bs[-20:]) - np.median(bs[:20]),
                     bar_min_delta=bs.min() - np.median(bs[:20]))
    else:
        bar_f = dict(bar_range=np.nan, bar_net=np.nan, bar_min_delta=np.nan)
    rows.append(dict(
        file=b, subject=s, device=DEV[d], activity_id=a, activity=ACT.get(a, "Unknown"), trial=t,
        is_fall=int(a >= 100),
        acc_max=am.max(), acc_min=am.min(), acc_std=am.std(), acc_p95=np.percentile(am, 95), jerk_max=jerk.max(),
        gyr_max=gm.max(), gyr_std=gm.std(), gyr_p95=np.percentile(gm, 95), **bar_f))
df = pd.DataFrame(rows)

VAL, TEST = {7, 10, 13}, {2, 9, 14}
df["split"] = df.subject.map(lambda s: "test" if s in TEST else "val" if s in VAL else "train")
df.round(5).to_csv(out / "features.csv", index=False)
print(len(df), "trials;\n" + df.groupby(["split", "is_fall"]).size().unstack().to_string())
print("test subjects:", sorted(TEST), "val:", sorted(VAL))

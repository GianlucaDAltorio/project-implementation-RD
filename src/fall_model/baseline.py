"""Whole-trial baseline: random forest on trial-level features, waist vs wrist, with and
without the barometer.

For each setting the decision threshold is the highest one whose validation miss rate
is at most 5%; miss and false-alarm rates are then reported on the test subjects.

Usage:
    python baseline.py [features.csv]
"""
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier

ROOT = Path(__file__).resolve().parents[2]
df = pd.read_csv(sys.argv[1] if len(sys.argv) > 1 else ROOT / "data" / "features" / "features.csv")

IMU = ["acc_max", "acc_min", "acc_std", "acc_p95", "jerk_max", "gyr_max", "gyr_std", "gyr_p95"]
BAR = ["bar_range", "bar_net", "bar_min_delta"]


def rates(y, p):
    miss = ((p == 0) & (y == 1)).sum() / max((y == 1).sum(), 1)
    fa = ((p == 1) & (y == 0)).sum() / max((y == 0).sum(), 1)
    return miss, fa


res = []
for dev in ["Waist", "Wrist"]:
    d = df[df.device == dev]
    tr, va, te = (d[d.split == k] for k in ("train", "val", "test"))
    for name, feats in (("IMU only", IMU), ("IMU + barometer", IMU + BAR)):
        rf = RandomForestClassifier(300, class_weight="balanced", min_samples_leaf=2,
                                    random_state=0, n_jobs=2).fit(tr[feats], tr.is_fall)
        pv = rf.predict_proba(va[feats])[:, 1]
        ok = [t for t in np.linspace(0.02, 0.98, 49)
              if rates(va.is_fall.values, (pv >= t).astype(int))[0] <= 0.05]
        thr = max(ok or [0.02])
        pt = rf.predict_proba(te[feats])[:, 1]
        miss, fa = rates(te.is_fall.values, (pt >= thr).astype(int))
        res.append(dict(device=dev, features=name, thr=round(thr, 2),
                        test_falls=int(te.is_fall.sum()), test_nonfalls=int((1 - te.is_fall).sum()),
                        miss=round(miss, 3), false_alarm=round(fa, 3)))
print(pd.DataFrame(res).to_string(index=False))

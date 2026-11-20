"""Impact windows with wrist and waist features, for the sensor fusion models.

Uses FallAllD trials recorded on both the wrist (device 2) and the waist (device 3). A window
opens where the wrist |a| reaches 2.5 g, as on the watch, and the same 14 features are
computed for the wrist (w_) and for the waist (z_). The waist window is centred on the waist's
own |a| peak within 0.25 s of the wrist peak, as the waist board does. The waist barometer
adds dh_m, the height change across that peak. Labels and the subject split follow
wrist_windows.py and build_features.py. Writes data/features/fusion_windows.csv.

Usage: python fusion_windows.py [subset_dir]    (default data/fallalld/my_subset_fusion)
"""
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd

from load_wrist import FEATURES, FS, ROOT
from wrist_windows import POST, PRE, candidates, feats

BAR_FS = 10
ALIGN = FS // 4          # FD_ALIGN in fd_features.h
VAL, TEST = {7, 10, 13}, {2, 9, 14}


def height_m(p_hpa):
    return 44330.0 * (1.0 - (p_hpa / 1013.25) ** 0.1903)


def height_change(bar, i):
    """Median height 1-2 s after the impact minus 0.5-2 s before it, in metres.
    Same rule as fd_height_change() in src/FallDetector_trials/fd_features.c."""
    t = np.arange(len(bar)) / BAR_FS - i / FS
    before, after = bar[(t >= -2.0) & (t < -0.5)], bar[(t >= 1.0) & (t < 2.0)]
    if len(before) == 0 or len(after) == 0:
        return np.nan
    return height_m(np.median(after)) - height_m(np.median(before))


def load(path, scale):
    return np.loadtxt(path, delimiter=",") * scale


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data" / "fallalld" / "my_subset_fusion"
    rows, trials = [], []
    for fw in sorted(src.glob("S*_D2_A*_T*_A.dat")):
        s, a, t = map(int, re.match(r"S(\d+)_D2_A(\d+)_T(\d+)_A\.dat$", fw.name).groups())
        bw, bz = fw.name[:-6], fw.name[:-6].replace("_D2_", "_D3_")
        if not (src / f"{bz}_A.dat").exists() or not (src / f"{bz}_B.dat").exists():
            continue
        wa, wg = load(fw, 0.000244), load(src / f"{bw}_G.dat", 0.07)
        za, zg = load(src / f"{bz}_A.dat", 0.000244), load(src / f"{bz}_G.dat", 0.07)
        bar = np.loadtxt(src / f"{bz}_B.dat", delimiter=",")[:, 0]
        n = min(len(wa), len(za))
        split = "test" if s in TEST else "val" if s in VAL else "train"
        trials.append(dict(trial=bw.replace("_D2", ""), subject=s, activity_id=a,
                           is_fall=int(a >= 100), split=split))
        zm = np.linalg.norm(za, axis=1)
        for i in candidates(np.linalg.norm(wa[:n], axis=1)):
            if i - ALIGN < 0 or i + ALIGN >= len(za):
                continue
            j = i - ALIGN + int(np.argmax(zm[i - ALIGN:i + ALIGN + 1]))
            if j - PRE < 0 or j + POST > len(za):
                continue
            r = {f"w_{k}": v for k, v in feats(wa, wg, i).items()}
            r.update({f"z_{k}": v for k, v in feats(za, zg, j).items()})
            r.update(dh_m=height_change(bar, j), trial=trials[-1]["trial"], subject=s, activity_id=a,
                     is_fall_trial=int(a >= 100), split=split, t_s=round(i / FS, 2), idx=int(i),
                     label=int(a >= 100 and abs(i / FS - 10) < 1.5))
            rows.append(r)
    W, T = pd.DataFrame(rows), pd.DataFrame(trials)
    W.round(4).to_csv(FEATURES / "fusion_windows.csv", index=False)
    T.to_csv(FEATURES / "fusion_trials.csv", index=False)
    print(len(T), "paired trials (", int(T.is_fall.sum()), "falls ),", T.subject.nunique(), "subjects")
    print(len(W), "windows,", int(W.label.sum()), "positive")
    f = W[W.label == 1].dh_m
    print("height change at fall impacts: median %.2f m; at other windows: median %.2f m"
          % (f.median(), W[W.label == 0].dh_m.median()))


if __name__ == "__main__":
    main()

"""Cut impact-triggered windows from the wrist recordings and compute their features.

A window is opened wherever the acceleration magnitude reaches 2.5 g (triggers closer
than 2 s to a stronger one are skipped) and covers 1 s before and 2 s after the impact.
Each window gets the 14 features used on the watch. A window is labelled positive when
it comes from a fall trial and the impact lies within 1.5 s of the recorded fall time
(10 s into the trial).

The feature definitions here are mirrored in src/FallDetector_trials/fall_detect.c;
changing one requires changing the other.

Usage:
    python wrist_windows.py

Writes data/features/wrist_windows.csv.
"""
import numpy as np
import pandas as pd

from load_wrist import FEATURES, FS, load

TRIG = 2.5
PRE = int(1.0 * FS)
POST = int(2.0 * FS)
SEP = int(2.0 * FS)


def candidates(am):
    picked = []
    for i in np.argsort(-am):
        if am[i] < TRIG:
            break
        if i - PRE < 0 or i + POST > len(am):
            continue
        if all(abs(i - j) >= SEP for j in picked):
            picked.append(i)
    return sorted(picked)


def feats(a, g, i):
    sl = slice(i - PRE, i + POST)
    a, g = a[sl], g[sl]
    am, gm = np.linalg.norm(a, axis=1), np.linalg.norm(g, axis=1)
    p = PRE
    pre, post = am[:p], am[p + FS // 2:]
    ab, aa = a[:FS // 2].mean(0), a[-FS // 2:].mean(0)
    cosang = np.clip(ab @ aa / (np.linalg.norm(ab) * np.linalg.norm(aa) + 1e-9), -1, 1)
    return dict(
        peak=am[p - FS // 10:p + FS // 10].max(), pre_min=pre.min(), pre_std=pre.std(), pre_max=pre.max(),
        post_dev=np.abs(post - 1).mean(), post_std=post.std(), post_max=post.max(),
        jerk=np.abs(np.diff(am)).max() * FS, gyr_peak=gm.max(), gyr_pre=gm[:p].mean(), gyr_post=gm[p:].mean(),
        gyr_post_late=gm[p + FS // 2:].mean(), tilt_deg=np.degrees(np.arccos(cosang)),
        dur_hi=(am > 1.8).sum() / FS)


if __name__ == "__main__":
    meta, A, G = load()
    rows = []
    for k, r in meta.iterrows():
        am = np.linalg.norm(A[k], axis=1)
        for i in candidates(am):
            f = feats(A[k], G[k], i)
            f.update(file=r.file, subject=r.subject, activity_id=r.activity_id, activity=r.activity,
                     is_fall_trial=r.is_fall, t_s=round(i / FS, 2), idx=int(i),
                     label=int(r.is_fall == 1 and abs(i / FS - 10) < 1.5))
            rows.append(f)
    W = pd.DataFrame(rows)
    W.round(4).to_csv(FEATURES / "wrist_windows.csv", index=False)
    falls = meta[meta.is_fall == 1]
    print(len(W), "windows;", int(W.label.sum()), "positive;",
          "fall trials with a trigger:", round(W[W.label == 1].file.nunique() / len(falls), 3))
    print("non-fall trials with >=1 trigger:",
          round(W[W.is_fall_trial == 0].file.nunique() / (meta.is_fall == 0).sum(), 3))

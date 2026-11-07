"""Evaluate the windowed wrist model.

Training and validation subjects are cross-validated by subject (6 folds) to choose the
decision threshold: the one with the lowest false-alarm rate whose trial-level miss rate
is at most 5%. A final forest is then fitted on those subjects and scored once on the
held-out test subjects. A trial counts as detected if any of its windows reaches the
threshold, and a trial with no trigger raises no alarm.

Usage:
    python wrist_classify.py
"""
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import GroupKFold

from load_wrist import FEATURES

W = pd.read_csv(FEATURES / "wrist_windows.csv")
meta = pd.read_csv(FEATURES / "features.csv")
meta = meta[meta.device == "Wrist"]
F = [c for c in W.columns if c not in ("file", "subject", "activity_id", "activity", "is_fall_trial", "t_s", "idx", "label")]
W["split"] = W.subject.map(meta.drop_duplicates("subject").set_index("subject").split)


def make_rf():
    return RandomForestClassifier(100, max_depth=6, min_samples_leaf=3, class_weight="balanced",
                                  random_state=0, n_jobs=2)


def trial_rates(Wd, p, thr, meta_d):
    d = Wd.assign(alarm=(p >= thr))
    t = meta_d.set_index("file")
    alarm = d.groupby("file").alarm.any().reindex(t.index).fillna(False)
    f = t.is_fall == 1
    return 1 - alarm[f].mean(), alarm[~f].mean(), int(f.sum()), int((~f).sum())


dev = W[W.split != "test"]
md = meta[meta.split != "test"]
oof = np.zeros(len(dev))
for tr, te in GroupKFold(6).split(dev, groups=dev.subject):
    oof[te] = make_rf().fit(dev.iloc[tr][F], dev.iloc[tr].label).predict_proba(dev.iloc[te][F])[:, 1]

best = None
for thr in np.linspace(0.02, 0.9, 45):
    m, fa, _, _ = trial_rates(dev, oof, thr, md)
    if m <= 0.05 and (best is None or fa < best[1]):
        best = (thr, fa, m)
print("threshold chosen on dev OOF (miss<=5%):", None if best is None else tuple(round(x, 3) for x in best))
thr = best[0] if best else 0.1
print("dev OOF at thr: miss %.3f  false-alarm %.3f" % trial_rates(dev, oof, thr, md)[:2])

rf = make_rf().fit(dev[F], dev.label)
te = W[W.split == "test"]
mt = meta[meta.split == "test"]
m, fa, nf, nn = trial_rates(te, rf.predict_proba(te[F])[:, 1], thr, mt)
print(f"TEST subjects: miss {m:.3f} ({nf} falls)  false-alarm {fa:.3f} ({nn} non-falls)")
print(pd.Series(rf.feature_importances_, F).sort_values(ascending=False).round(3).head(8).to_string())

d = dev.assign(alarm=oof >= thr)
t = md.set_index("file").assign(alarm=d.groupby("file").alarm.any()).fillna({"alarm": False})
print(t[t.is_fall == 0].groupby("activity").alarm.mean().sort_values(ascending=False).head(8).round(2).to_string())

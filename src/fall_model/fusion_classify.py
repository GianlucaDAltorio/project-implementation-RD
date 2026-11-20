"""Compares wrist-only, waist-only and fused fall models on the same paired trials.

Each model is a random forest like the deployed wrist model. A trial counts as an alarm
if any of its windows reaches the threshold. Two evaluations:
1. Fixed split: threshold chosen by 6-fold cross-validation over the training and
   validation subjects (miss rate 5% or less), then scored once on the test subjects.
2. Leave one subject out: every trial is scored by a model that never saw that person,
   and the models are compared by their false-alarm rate at the same miss rate.
Writes data/features/fusion_results.csv and fusion_matched.csv.

Usage: python fusion_classify.py
"""
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import GroupKFold

from load_wrist import FEATURES

W = pd.read_csv(FEATURES / "fusion_windows.csv")
T = pd.read_csv(FEATURES / "fusion_trials.csv")
WRIST = [c for c in W.columns if c.startswith("w_")]
WAIST = [c for c in W.columns if c.startswith("z_")]
MODELS = {
    "wrist only": WRIST,
    "waist only": WAIST + ["dh_m"],
    "wrist + waist": WRIST + WAIST,
    "wrist + waist + barometer": WRIST + WAIST + ["dh_m"],
}


def make_rf():   # same settings as the deployed wrist model
    return RandomForestClassifier(100, max_depth=6, min_samples_leaf=3, class_weight="balanced",
                                  random_state=0, n_jobs=2)


def rates(win, p, thr, trials):
    alarm = win.assign(a=p >= thr).groupby("trial").a.any().reindex(trials.trial).fillna(False).values
    f = trials.is_fall.values == 1
    return 1 - alarm[f].mean(), alarm[~f].mean()


def evaluate(cols):
    X = W[cols].fillna(W[cols].median())
    dev, test = W.split != "test", W.split == "test"
    Tdev, Ttest = T[T.split != "test"], T[T.split == "test"]
    oof = np.zeros(dev.sum())
    for tr, te in GroupKFold(6).split(X[dev], groups=W[dev].subject):
        rf = make_rf().fit(X[dev].iloc[tr], W[dev].label.iloc[tr])
        oof[te] = rf.predict_proba(X[dev].iloc[te])[:, 1]
    best = None
    for thr in np.linspace(0.02, 0.9, 45):
        m, fa = rates(W[dev], oof, thr, Tdev)
        if m <= 0.05 and (best is None or fa < best[1]):
            best = (thr, fa)
    thr = best[0] if best else 0.1
    rf = make_rf().fit(X[dev], W[dev].label)
    m, fa = rates(W[test], rf.predict_proba(X[test])[:, 1], thr, Ttest)
    top = pd.Series(rf.feature_importances_, cols).sort_values(ascending=False).head(3)
    return dict(threshold=round(thr, 2), test_miss=round(m, 3), test_false_alarm=round(fa, 3),
                top_features=", ".join(top.index))


def loso_scores(cols):
    """Highest window probability per trial (0 if no window), each subject scored by a model
    trained on the others. Returns fall-trial scores (sorted) and non-fall-trial scores."""
    X = W[cols].fillna(W[cols].median())
    p = np.zeros(len(W))
    for s in W.subject.unique():
        tr, te = W.subject != s, W.subject == s
        p[te.values] = make_rf().fit(X[tr], W.label[tr]).predict_proba(X[te])[:, 1]
    score = W.assign(p=p).groupby("trial").p.max().reindex(T.trial).fillna(0).values
    return np.sort(score[T.is_fall.values == 1]), score[T.is_fall.values == 0]


def threshold_at(falls, miss):
    """Highest threshold that misses at most `miss` of the falls (0 if not reachable)."""
    return falls[int(miss * len(falls))]


def matched(cols, misses=(0.05, 0.10, 0.15)):
    f, n = loso_scores(cols)
    out = {}
    for m in misses:
        thr = threshold_at(f, m)
        out[f"false_alarm_at_{int(m * 100)}pct_miss"] = round((n >= thr).mean(), 3) if thr > 0 else np.nan
    return out


if __name__ == "__main__":
    t = T[T.split == "test"]
    print(f"test subjects: {int(t.is_fall.sum())} fall trials, {int((t.is_fall == 0).sum())} non-fall trials")
    res = pd.DataFrame([dict(model=k, n_features=len(v), **evaluate(v)) for k, v in MODELS.items()])
    res.to_csv(FEATURES / "fusion_results.csv", index=False)
    print(res.to_string(index=False))

    print(f"\nLeave one subject out: {int(T.is_fall.sum())} fall trials, {int((T.is_fall == 0).sum())} non-fall trials, "
          f"{T.subject.nunique()} subjects")
    print("false-alarm rate when each model misses at most 5%, 10% and 15% of falls:")
    mt = pd.DataFrame([dict(model=k, **matched(v)) for k, v in MODELS.items()])
    mt.to_csv(FEATURES / "fusion_matched.csv", index=False)
    print(mt.to_string(index=False))
    w = W.groupby("trial").size().reindex(T.trial).isna() & (T.is_fall.values == 1)
    print(f"(fall trials where the wrist never reached the trigger: {int(w.sum())}; no model can catch these)")

"""Make clean copies of the watch's events_nnn.csv session logs.

A trial runs from its MARK row until the next MARK, at most the trial window set for
that session below (the same rule as analyze_session.py). The clean copy keeps the START row
plus the MARK, IMPACT and CANCEL rows that fall inside a confirmed trial. Dropped:
  - trials listed in SESSIONS below (accidental button presses)
  - trials voided on the watch (VOID rows)
  - impacts and cancel presses outside every kept trial window (watch handling between trials)
Repeat FALL calls inside a kept window are retained. A session that contains both trial types
is written as two files, <name>_falls_clean.csv and <name>_nonfalls_clean.csv; a session with
only one type keeps a single <name>_clean.csv. Originals are not modified.

Usage:
    python clean_sessions.py

Reads data/sessions/original, writes data/sessions/clean/.
"""
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
ORIG = ROOT / "data" / "sessions" / "original"
CLEAN = ROOT / "data" / "sessions" / "clean"
DEFAULT_WINDOW_S = 15     # firmware trial window

# session file -> trials to drop and, if different from the default, the trial window in seconds
SESSIONS = {
    "events_001.csv": {"void": [16], "window_s": 30},   # trial 16: accidental double press
    "events_002.csv": {"void": [], "window_s": 30},
    "r2_events_001.csv": {"void": [12, 44]},
}


def clean(src, void, window_s):
    ev = pd.read_csv(src)
    W = window_s * 1000
    marks = ev[ev.kind == "MARK"].sort_values("boot_ms").reset_index(drop=True)
    drop = set(void) | set(ev[ev.kind == "VOID"]["id"].astype(int))
    t_end = ev.boot_ms.max() + 1
    start = ev.kind.eq("START")
    keep_fall, keep_non = start.copy(), start.copy()
    kept_trials = 0
    for i, m in marks.iterrows():
        if int(m.id) in drop:
            continue
        t0 = m.boot_ms
        t1 = min(marks.boot_ms.iloc[i + 1] if i + 1 < len(marks) else t_end, t0 + W)
        inside = (ev.boot_ms >= t0) & (ev.boot_ms < t1) & ev.kind.isin(["MARK", "IMPACT", "CANCEL"])
        if str(m.called).strip().upper() == "FALLTRIAL":
            keep_fall |= inside
        else:
            keep_non |= inside
        kept_trials += 1
    keep = keep_fall | keep_non
    lost = ev[~keep & ev.kind.isin(["MARK", "IMPACT", "CANCEL"])]
    return {"falls": ev[keep_fall], "nonfalls": ev[keep_non]}, ev[keep], lost, kept_trials


if __name__ == "__main__":
    CLEAN.mkdir(parents=True, exist_ok=True)
    for name, cfg in SESSIONS.items():
        if not (ORIG / name).exists():
            continue
        parts, out, lost, n = clean(ORIG / name, cfg.get("void", []), cfg.get("window_s", DEFAULT_WINDOW_S))
        stem = Path(name).stem
        for old in CLEAN.glob(f"{stem}*_clean.csv"):
            old.unlink()
        has = {k: (v.kind == "MARK").any() for k, v in parts.items()}
        if all(has.values()):
            names = []
            for k, v in parts.items():
                v.to_csv(CLEAN / f"{stem}_{k}_clean.csv", index=False)
                names.append(f"{stem}_{k}_clean.csv")
            dst = " + ".join(names)
        else:
            out.to_csv(CLEAN / f"{stem}_clean.csv", index=False)
            dst = f"{stem}_clean.csv"
        lf = int(((lost.kind == "IMPACT") & (lost.called == "FALL")).sum())
        print(f"{name}: kept {n} trials, {len(out)} rows -> {dst}; dropped {len(lost)} rows "
              f"({int((lost.kind == 'IMPACT').sum())} impacts, {lf} of them FALL calls, outside the confirmed windows)")

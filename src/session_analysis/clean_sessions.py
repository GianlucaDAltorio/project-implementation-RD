"""Make clean copies of the watch's events_nnn.csv session logs.

A trial runs from its MARK row until the next MARK, at most WINDOW_S seconds (the same
rule as analyze_session.py). The clean copy keeps the START row plus the MARK, IMPACT
and CANCEL rows that fall inside a confirmed trial. Dropped:
  - trials listed in SESSIONS below (accidental button presses)
  - trials voided on the watch (VOID rows)
  - impacts and cancel presses outside every kept trial window (watch handling between trials)
Repeat FALL calls inside a kept window are retained. Originals are not modified.

Usage:
    python clean_sessions.py

Reads data/sessions/original, writes data/sessions/clean/<name>_clean.csv.
"""
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
ORIG = ROOT / "data" / "sessions" / "original"
CLEAN = ROOT / "data" / "sessions" / "clean"
WINDOW_S = 30

# session file -> trial numbers to drop
SESSIONS = {
    "events_001.csv": [16],   # accidental double press; trial 16 has no impact, trial 17 is the real one
    "events_002.csv": [],
}


def clean(src, void):
    ev = pd.read_csv(src)
    W = WINDOW_S * 1000
    marks = ev[ev.kind == "MARK"].sort_values("boot_ms").reset_index(drop=True)
    drop = set(void) | set(ev[ev.kind == "VOID"]["id"].astype(int))
    t_end = ev.boot_ms.max() + 1
    keep = ev.kind.eq("START")
    kept_trials = 0
    for i, m in marks.iterrows():
        if int(m.id) in drop:
            continue
        t0 = m.boot_ms
        t1 = min(marks.boot_ms.iloc[i + 1] if i + 1 < len(marks) else t_end, t0 + W)
        keep |= (ev.boot_ms >= t0) & (ev.boot_ms < t1) & ev.kind.isin(["MARK", "IMPACT", "CANCEL"])
        kept_trials += 1
    lost = ev[~keep & ev.kind.isin(["MARK", "IMPACT", "CANCEL"])]
    return ev[keep], lost, kept_trials


if __name__ == "__main__":
    CLEAN.mkdir(parents=True, exist_ok=True)
    for name, void in SESSIONS.items():
        out, lost, n = clean(ORIG / name, void)
        dst = CLEAN / (Path(name).stem + "_clean.csv")
        out.to_csv(dst, index=False)
        lf = int(((lost.kind == "IMPACT") & (lost.called == "FALL")).sum())
        print(f"{name}: kept {n} trials, {len(out)} rows -> {dst.name}; dropped {len(lost)} rows "
              f"({int((lost.kind == 'IMPACT').sum())} impacts, {lf} of them FALL calls, outside the confirmed windows)")

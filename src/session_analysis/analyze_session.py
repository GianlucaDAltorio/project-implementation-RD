"""Score test sessions from the watch's event logs.

The watch records the trial type itself (one BOOT press = real-fall trial, two presses =
non-fall trial), so no separate label file is needed. A trial runs from its MARK row until
the next MARK, at most --window seconds (default 30, the same as the on-screen banner).

    real-fall trial  with a FALL call inside -> detected,    none -> MISSED
    non-fall trial   with a FALL call inside -> FALSE ALARM, none -> correct

Usage:
    python analyze_session.py [events.csv ...] [--notes notes.csv] [--window 30] [--void N ...]

With no file arguments every *_clean.csv in data/sessions/clean is scored, followed by
combined totals. --void ignores the given trial numbers. notes.csv has the columns
mark,note and adds a description column to the table (see notes_template.csv).
"""
import argparse, sys
from pathlib import Path
import pandas as pd

ap = argparse.ArgumentParser()
ap.add_argument("events", nargs="*"); ap.add_argument("--notes"); ap.add_argument("--window", type=float, default=30.0)
ap.add_argument("--void", type=int, nargs="*", default=[])
args = ap.parse_args()
CLEAN = Path(__file__).resolve().parents[2] / "data" / "sessions" / "clean"
files = args.events or sorted(CLEAN.glob("*_clean.csv"))
if not files:
    sys.exit(f"no *_clean.csv files found in {CLEAN}")
W = args.window * 1000
notes = {}
if args.notes:
    nt = pd.read_csv(args.notes)
    notes = dict(zip(nt.mark.astype(int), nt.note))


def score(path):
    ev = pd.read_csv(path)
    if ev.empty or "kind" not in ev: sys.exit(f"{path}: empty events file")
    marks = ev[ev.kind == "MARK"].copy()
    if marks.empty: sys.exit(f"{path}: no MARK rows - nothing to score")
    marks["mark"] = marks["id"].astype(int)
    marks = marks.sort_values("boot_ms").reset_index(drop=True)
    voided = set(ev[ev.kind == "VOID"]["id"].astype(int)) | set(args.void)
    impacts = ev[ev.kind == "IMPACT"]
    falls = impacts[impacts.called == "FALL"]
    t_end = ev.boot_ms.max() + 1
    rows, covered = [], pd.Series(False, index=falls.index)
    for i, m in marks.iterrows():
        t0 = m.boot_ms
        t1 = min(marks.boot_ms.iloc[i + 1] if i + 1 < len(marks) else t_end, t0 + W)
        inwin = (falls.boot_ms >= t0) & (falls.boot_ms < t1)
        covered |= inwin
        if int(m.mark) in voided: continue
        is_fall = str(m.called).strip().upper() == "FALLTRIAL"
        imp = impacts[(impacts.boot_ms >= t0) & (impacts.boot_ms < t1)]
        called = int(inwin.sum())
        rows.append(dict(file=Path(path).name, trial=int(m.mark), type="REAL FALL" if is_fall else "NON-FALL",
                         note=notes.get(int(m.mark), ""), impacts=len(imp),
                         max_p=round(float(imp.p.max()), 2) if len(imp) else None, fall_calls=called,
                         repeat_calls=max(called - 1, 0),
                         result=("detected" if called else "MISSED") if is_fall else ("FALSE ALARM" if called else "correct")))
    return pd.DataFrame(rows), int((~covered).sum()), int((ev.kind == "CANCEL").sum())


def summary(r, outside=None, cancels=None):
    nf, nn = int((r.type == "REAL FALL").sum()), int((r.type == "NON-FALL").sum())
    miss, fa = int((r.result == "MISSED").sum()), int((r.result == "FALSE ALARM").sum())
    print(f"REAL FALL trials: {nf}   missed: {miss}" + (f"   miss rate: {miss/nf:.1%}   (target <= 5%)" if nf else ""))
    print(f"NON-FALL trials:  {nn}   false alarms: {fa}" + (f"   false-alarm rate: {fa/nn:.1%}   (target < 10%)" if nn else ""))
    print(f"extra (repeat) FALL calls inside trials: {int(r.repeat_calls.sum())}")
    if outside is not None: print(f"FALL calls outside every trial window: {outside}   cancel presses: {cancels}")


allr = []
for f in files:
    r, outside, cancels = score(f)
    if r.empty: print(f"{f}: every trial was voided"); continue
    if not r.note.any(): r = r.drop(columns="note")
    print(f"\n=== {Path(f).name} ===")
    print(r.drop(columns="file").to_string(index=False)); print()
    summary(r, outside, cancels)
    allr.append(r)
if len(allr) > 1:
    print("\n=== ALL FILES COMBINED ==="); summary(pd.concat(allr))
print("\nSmall sample: quote the counts as well as the rates.")

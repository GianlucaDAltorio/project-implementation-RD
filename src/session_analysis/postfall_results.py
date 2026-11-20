"""Cleans the post-fall check sessions (REQ-FD-06) and writes one row per trial.

Trial types come from the test plan (SESSIONS), not the log. Voided falls are dropped
with their CANCEL and MARK rows. Writes to data/sessions/postfall/clean.

Usage: python postfall_results.py
"""
from pathlib import Path
import pandas as pd

ROOT = Path(__file__).resolve().parents[2] / "data" / "sessions" / "postfall"
ESCALATE_S = 60

# session -> fall id: "still" (should escalate) or "recovery" (should not), and voided falls
SESSIONS = {
    "001_alert": {"trials": {1: "still", 2: "recovery", 3: "still", 6: "recovery", 7: "still",
                             8: "recovery", 9: "still", 10: "recovery", 12: "still"},
                  "void": [4, 5, 11]},   # 4 and 5: second fall triggered while moving; 11: trial started by accident
    "002_alert": {"trials": {1: "recovery"}, "void": []},   # extra recovery trial to make 5 of each
}


def clean(session, trials):
    ev = pd.read_csv(ROOT / "original" / f"events_{session}.csv")
    pi = pd.read_csv(ROOT / "original" / f"alerts_log_{session}.csv")

    # each CANCEL/MARK belongs to the last fall before it
    ev["fall"] = ev["id"].where(ev.kind.isin(["IMPACT", "ALERT", "POSTFALL", "RESOLVE"])).ffill()
    keep = ev.kind.eq("START") | (ev.fall.isin(trials) & ~ev.kind.isin(["CANCEL", "MARK"]))
    ev = ev[keep]
    ev.drop(columns="fall").to_csv(ROOT / "clean" / f"events_{session}_clean.csv", index=False)

    pi["fall"] = pi.aid.str.split("-").str[1].astype(int)
    pi = pi[pi.fall.isin(trials)]
    pi.drop(columns="fall").to_csv(ROOT / "clean" / f"alerts_log_{session}_clean.csv", index=False)
    return ev, pi


def main():
    rows = []
    for session, cfg in SESSIONS.items():
        ev, pi = clean(session, cfg["trials"])
        for fid, kind in cfg["trials"].items():
            e = ev[ev.fall == fid]
            imp = e[e.kind == "IMPACT"].iloc[0]
            pf = e[e.kind == "POSTFALL"].iloc[0]
            res = e[e.kind == "RESOLVE"]
            p = pi[pi.fall == fid].set_index("event")
            esc = pf.called == "ESCALATED"
            expected = "ESCALATED" if kind == "still" else "MOVED"
            rows.append({
                "trial": len(rows) + 1, "session": session, "fall_id": fid, "type": kind,
                "peak_g": imp.peak_g, "p": imp.p, "expected": expected, "outcome": pf.called,
                "pass": pf.called == expected,
                "escalation_s": pf.peak_g if esc else None,
                "last_move_s": pf.peak, "still_s": pf.pre_min, "moving_s": pf.p,
                "alert_delay_pi_s": p.delay_s.get("fall"),
                "escalation_delay_pi_s": p.delay_s.get("escalate") if esc else None,
                "ok_pressed_s": res.peak_g.iloc[0] if len(res) else None,
                "ok_reached_pi": "cancel" in p.index,
                "rssi_dbm": p.rssi.get("fall"),
            })
    out = pd.DataFrame(rows)
    out.to_csv(ROOT / "clean" / "postfall_trials.csv", index=False)

    print(out.to_string(index=False))
    for kind in ("still", "recovery"):
        t = out[out.type == kind]
        print(f"\n{kind}: {int(t['pass'].sum())}/{len(t)} as expected")
    s = out[out.type == "still"]
    print(f"escalation after detection: {s.escalation_s.min():.1f}-{s.escalation_s.max():.1f} s "
          f"(stillness at escalation {s.still_s.min():.1f}-{s.still_s.max():.1f} s, rule {ESCALATE_S} s)")
    print(f"fall alert at the Pi: {out.alert_delay_pi_s.min():.2f}-{out.alert_delay_pi_s.max():.2f} s after detection")


if __name__ == "__main__":
    main()

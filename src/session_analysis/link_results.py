"""Alert delivery results for the link tests (REQ-FD-02).

Lists every message the watch should have sent, from the SD log, and checks whether and
when the Pi received it, so lost messages show up. Writes one row per message and a
summary per run to data/sessions/link/clean.

Usage: python link_results.py
"""
from pathlib import Path
import pandas as pd

ROOT = Path(__file__).resolve().parents[2] / "data" / "sessions" / "link"
LIMIT_S = 25

# run -> firmware, Pi log, and the SD logs with the boot id that prefixes their aid
RUNS = {
    "r1_strong":      ("no ack", "alerts_log_r1.csv", {"events_r1_strong.csv": "741c"}),
    "r1_weak":        ("no ack", "alerts_log_r1.csv", {"events_r1_weak_a.csv": "f04e",
                                                       "events_r1_weak_b.csv": "2b68"}),
    "r2_weak":        ("ack, net priority 1", "alerts_log_r2.csv", {"events_r2_weak_a.csv": "2cfb",
                                                                    "events_r2_weak_b.csv": "e29f"}),
    "r3_moderate":    ("ack, net priority 0", "alerts_log_r3.csv", {"events_r3_moderate.csv": "d4e8"}),
    "outage_stop":    ("ack, net priority 0", "alerts_log_outage_stop.csv", {"events_outage_stop.csv": "b1e7"}),
    "outage_suspend": ("ack, net priority 0", "alerts_log_outage_suspend.csv", {"events_outage_suspend.csv": "3163"}),
}
SENT = {"fall": "SENT", "escalate": "ESC_SENT", "cancel": "OK_SENT"}


def expected_messages(ev):
    """(fall id, event, detection ms) for every message the watch should have sent."""
    out, falls, last = [], {}, None
    for r in ev.itertuples():
        if r.kind == "IMPACT" and r.called == "FALL":
            last = int(r.id); falls[last] = r.boot_ms
            out.append([last, "fall", r.boot_ms])
        elif r.kind == "CANCEL" and last is not None:      # cancelled in the window: nothing sent
            out = [m for m in out if not (m[0] == last and m[1] == "fall")]
        elif r.kind == "POSTFALL" and r.called == "ESCALATED":
            out.append([int(r.id), "escalate", falls[int(r.id)]])
        elif r.kind == "RESOLVE":
            out.append([int(r.id), "cancel", falls[int(r.id)]])
    return out


def main():
    rows, summary = [], []
    for run, (fw, pi_name, sessions) in RUNS.items():
        pi = pd.read_csv(ROOT / "original" / pi_name)
        pi["boot"] = pi.aid.str.split("-").str[0]
        pi["fall"] = pi.aid.str.split("-").str[1].astype(int)
        restarts = []
        for i, (name, boot) in enumerate(sessions.items()):
            ev = pd.read_csv(ROOT / "original" / name)
            if i > 0:                                         # a later session means the watch restarted
                why = ev[ev.kind == "START"].called.iloc[0]
                restarts.append(why if isinstance(why, str) else "reason not logged")
            for fall, event, det in expected_messages(ev):
                got = pi[(pi.boot == boot) & (pi.fall == fall) & (pi.event == event)]
                sd = ev[(ev.kind == "ALERT") & (ev.id == fall) & (ev.called == SENT[event])]
                rows.append({
                    "run": run, "firmware": fw, "session": name, "aid": f"{boot}-{fall}", "event": event,
                    "delivered": len(got) > 0,
                    "delay_s": got.delay_s.iloc[0] if len(got) else None,
                    "sends": sd.pre_min.iloc[0] if len(sd) and fw != "no ack" else None,
                    "rssi_dbm": got.rssi.iloc[0] if len(got) else None,
                })
        r = pd.DataFrame([x for x in rows if x["run"] == run])
        f = r[r.event == "fall"]
        summary.append({
            "run": run, "firmware": fw,
            "fall_alerts": f"{int(f.delivered.sum())}/{len(f)}",
            "all_messages": f"{int(r.delivered.sum())}/{len(r)}",
            "fall_delay_min_s": f.delay_s.min(), "fall_delay_median_s": f.delay_s.median(),
            "fall_delay_max_s": f.delay_s.max(),
            "fall_within_25s": int((f.delay_s <= LIMIT_S).sum()),
            "fall_delay_unknown": int((f.delivered & f.delay_s.isna()).sum()),   # no NTP time yet
            "max_sends": r.sends.max(),
            "rssi_min_dbm": f.rssi_dbm.min(), "rssi_max_dbm": f.rssi_dbm.max(),
            "restarts": ", ".join(restarts) if restarts else "none",
        })
    pd.DataFrame(rows).to_csv(ROOT / "clean" / "link_messages.csv", index=False)
    s = pd.DataFrame(summary)
    s.to_csv(ROOT / "clean" / "link_summary.csv", index=False)
    print(s.to_string(index=False))


if __name__ == "__main__":
    main()

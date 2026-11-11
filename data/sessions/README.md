# Watch test sessions

`original/` holds the `events_nnn.csv` files exactly as written to the SD card.
`clean/` holds the same sessions reduced to confirmed trial windows by
`src/session_analysis/clean_sessions.py`. A session with both trial types is split into
`_falls_clean.csv` and `_nonfalls_clean.csv`.

| Session | Model | Trial window | Trials kept | Result |
| --- | --- | --- | --- | --- |
| events_001 (round 1, real falls) | v1, threshold 0.26 | 30 s | 20 | 1 missed |
| events_002 (round 1, non-falls) | v1, threshold 0.26 | 30 s | 10 | 8 false alarms |
| r2_events_001 (round 2) | v2, threshold 0.30 | 15 s | 20 falls, 20 non-falls | 0 missed, 11 false alarms |

Round 2 also used the one-FALL-per-trial lockout (extra FALL calls are logged as `suppressed`).
Trials excluded from each session are listed in `clean_sessions.py`. Rows outside the confirmed
windows are removed from the clean files and stay in `original/`.

Raw IMU recordings (`raw_nnn.bin`) from the same sessions belong in `raw/` and convert with
`src/session_analysis/raw_to_csv.py`.

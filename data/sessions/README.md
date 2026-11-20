# Watch test sessions

Fall detection trials (rounds 1 and 2) are in this folder. The post-fall check trials are in
`postfall/` and the alert delivery tests in `link/`, each with its own README.

`original/` holds the `events_nnn.csv` files exactly as written to the SD card.
`clean/` holds the same sessions reduced to confirmed trial windows by
`src/session_analysis/clean_sessions.py`. A session with both trial types is split into
`_falls_clean.csv` and `_nonfalls_clean.csv`.

| Session | Model | Trial window | Trials kept | Result |
| --- | --- | --- | --- | --- |
| events_001 (round 1, real falls) | v1, threshold 0.26 | 30 s | 20 | 1 missed |
| events_002 (round 1, non-falls) | v1, threshold 0.26 | 30 s | 10 | 8 false alarms |
| r2_events_001 (round 2) | v2, threshold 0.30 | 15 s | 20 falls, 20 non-falls | 0 missed, 11 false alarms |
| r3_events_001 (round 3, real falls) | fused wrist + waist, threshold 0.348 | 15 s | 20 | 0 missed |
| r3_events_002 (round 3, non-falls) | fused wrist + waist, threshold 0.348 | 15 s | 20 | 11 false alarms (wrist model alone: 18) |

Round 2 also used the one-FALL-per-trial lockout (extra FALL calls are logged as `suppressed`).

Round 3 was run with the waist board taped to the waist. The waist board
answered every impact (median reply 340 ms, longest 476 ms), so every decision used the fused model.
Each impact row also logs `p_wrist`, the wrist-only model's probability, which `analyze_session.py`
scores as the `wrist_only` column. The non-fall trials were sitting down hard, swinging the arms hard,
and hitting the wrist on a table or the side of the body; which trial was which was not recorded.
Trials excluded from each session are listed in `clean_sessions.py`. Rows outside the confirmed
windows are removed from the clean files and stay in `original/`.

Raw IMU recordings go in `raw/` and convert with `src/session_analysis/raw_to_csv.py`.
`raw_postfall_001.bin` and `raw_link_r3_moderate.bin` are from the post-fall and link tests;
`raw_r3_001.bin` and `raw_r3_002.bin` are the watch recordings from round 3.

# Watch test sessions

`original/` holds the `events_nnn.csv` files exactly as written to the SD card.
`clean/` holds the same sessions reduced to confirmed trial windows by
`src/session_analysis/clean_sessions.py`.

| Session | Content | Trials kept | Removed |
| --- | --- | --- | --- |
| events_001 | real falls | 20 | trial 16 (accidental double press) |
| events_002 | non-falls (walking, scratching the head, jumping jacks, fast waving, sitting down hard) | 10 | none |

Rows outside the confirmed windows are removed from the clean files (5 FALL calls in total:
2 in events_001, 3 in events_002). They remain in `original/`.

Raw IMU recordings (`raw_nnn.bin`) from the same sessions belong in `raw/` and convert with
`src/session_analysis/raw_to_csv.py`.

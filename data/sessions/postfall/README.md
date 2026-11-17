# Post-fall check sessions (REQ-FD-06)

15 Nov 2026 (sessions 001 and 002), firmware `src/FallDetector_trials`. The watch was dropped onto a soft surface
and either left still (should escalate after 60 s) or picked up and kept moving (should not).

- `original/events_00n_alert.csv`: SD card event logs as recorded
- `original/alerts_log_00n_alert.csv`: what the Pi received (`screen.py`)
- `../raw/raw_postfall_001.bin`: every IMU sample of session 001
- `clean/`: the same logs without the voided falls, plus `postfall_trials.csv`, one row per trial

Session 002 is one extra recovery trial, run after session 001 to make 5 trials of each type.

Run `python src/session_analysis/postfall_results.py` to rebuild `clean/`. In session 001, falls 4, 5 and 11
were mistakes during the session (a second fall triggered while moving, and a trial started
by accident); both were redone.

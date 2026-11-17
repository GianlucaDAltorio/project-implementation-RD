# Alert delivery tests (REQ-FD-02)

15 to 17 Nov 2026. A fall was triggered by dropping the watch onto a soft surface; the alert
went over MQTT to the broker on the laptop and on to the Pi (`screen.py`). Hotspot, laptop
and Pi stayed together; only the watch moved.

| Run | Firmware | Conditions |
| --- | --- | --- |
| r1_strong | no ack | watch in the same room as the hotspot |
| r1_weak | no ack | watch in the basement; the watch restarted once (two SD sessions) |
| r2_weak | ack, network task priority 1 | basement again; restarted once (`WATCHDOG`) |
| r3_moderate | ack, network task priority 0 | basement, but a stronger signal than r1/r2 |
| outage_stop | ack, network task priority 0 | broker stopped for 10-15 s during each alert |
| outage_suspend | ack, network task priority 0 | broker suspended for about 20 s during each alert |

"ack" = the watch resends each message until the Pi acknowledges it (`pi/dashboard/ack`).

- `original/events_*.csv`: SD card event logs as recorded
- `original/alerts_log_*.csv`: what the Pi received. `alerts_log_r1.csv` covers both round 1 runs; `alerts_log_r3.csv` starts with one row from an earlier test, which the script ignores
- `../raw/raw_link_r3_moderate.bin`: every IMU sample of r3
- `clean/link_messages.csv`: one row per message the watch should have sent, with whether and when it reached the Pi
- `clean/link_summary.csv`: one row per run

Run `python src/session_analysis/link_results.py` to rebuild `clean/`. `delay_s` is detection
to arrival at the Pi and needs NTP time on both; the first r2 fall came before NTP had synced.

# project-implementation-RD

Repository for the research and development work on the 247-519-VA (Project I) project at Vanier College: an ESP32-based smartwatch and custom waist-worn system for fall detection and caregiver alerting.

## Contents

- `documents/proposal/` the Project Proposal (LaTeX, `Proposal.pdf`).
- `documents/gate1/` the Stage 1 Concept and Feasibility Report (LaTeX, `gate1_report.pdf`).
- `figures/generated/` the context diagram and the functional block diagrams.
- `src/FallDetector_trials/` watch firmware: 238 Hz IMU sampling, impact-triggered fall classifier, 15 s cancel window, MQTT alerts, 60 s post-fall check, SD logging.
- `src/WaistBoard/` waist board firmware (ESP32, MPU6050, BMP280) that sends its features to the watch over Bluetooth LE.
- `src/fall_model/` training pipeline for the wrist and fused classifiers and the C-versus-Python parity checks.
- `src/dashboard/` Raspberry Pi alert screen and the alert path setup.
- `src/session_analysis/` tools that clean and score the watch logs.
- `src/watch_WiFi_OLED_test/`, `src/watch_MQTT_test/`, `src/waist_sensor_test/` bring-up sketches for the watch and the waist board.
- `data/` model features, the watch test logs, and `fallalld/` instructions for recreating the FallAllD subset ([IEEE DataPort](https://ieee-dataport.org/open-access/fallalld-comprehensive-dataset-human-falls-and-activities-daily-living); the data itself is not committed).

Each folder has its own README with the details. `secrets.h` (Wi-Fi details) is not committed; copy `secrets_example.h` and fill it in.

## Results

Watch test sessions, one wearer, scripted falls and non-fall movements (`data/sessions/README.md`):

| Session | Model | Trials | Result |
| --- | --- | --- | --- |
| Round 1, real falls | v1 | 20 | 19 detected, 1 missed |
| Round 1, non-falls | v1 | 10 | 8 false alarms |
| Round 2, real falls | v2 | 20 | 20 detected |
| Round 2, non-falls | v2 | 20 | 11 false alarms |
| Round 3, real falls | fused wrist + waist | 20 | 20 detected |
| Round 3, non-falls | fused wrist + waist | 20 | 11 false alarms; the wrist model alone would have raised 18 |

The false-alarm rate is far above the under-10% target in every round. The round 3 movements were harsher than round 2's, so the two rounds' counts are not directly comparable, and the sample sizes are too small to show that the miss-rate target is met.

- **Alert delivery (REQ-FD-02):** every alert arrived over strong, moderate and weak links once the Pi acknowledged messages and the watch resent them. Broker outages of 10-20 s push delivery to 28-61 s. Details in `data/sessions/link/README.md`.
- **Post-fall check (REQ-FD-06):** on a dropped watch, 5 of 5 still trials escalated after 60 s and 5 of 5 recovery trials did not. Details in `data/sessions/postfall/README.md`.

## Reproducing

Requires Python 3 (`requirements.txt`) and gcc for the parity checks. Run everything from the repository root.

- Models: `src/fall_model/README.md`.
- Alert path (broker, watch, Pi): `src/dashboard/README.md`.
- Scoring the watch logs: `python src/session_analysis/clean_sessions.py`, then `python src/session_analysis/analyze_session.py`.

## Status

The proof-of-concept wrist detector runs on the watch and logs test trials to SD. The MQTT alert path to the Pi works, with acknowledgements and resending, and the 60 s post-fall check passed its 10 trials. With the waist board worn, the fused model detected 20 of 20 falls and cut false alarms from 18 to 11 of 20 compared with the wrist alone on the same movements. The false-alarm rate is still far above target.

# dashboard

`screen.py` runs on the Raspberry Pi and shows alerts from the watch in the terminal.
`publisher.py` runs on the laptop and sends test messages to check the broker link.

Set `BROKER` in `screen.py` to the laptop's IPv4 address, then on the Pi:

    pip install paho-mqtt
    python3 screen.py

Every fall, escalate and cancel message is appended to `alerts_log.csv` next to the script,
with the Pi's receive time and `delay_s` (receive time minus detection time on the watch).
`delay_s` is only valid when the watch and the Pi both have NTP time.

# dashboard

`screen.py` runs on the Raspberry Pi and shows alerts from the watch in the terminal.
`publisher.py` runs on the laptop and sends test messages to check the broker link.

## Alert path setup

Watch → MQTT over Wi-Fi → Mosquitto broker on the laptop → Raspberry Pi, which subscribes to `pi/dashboard/#` and shows the alert.

1. Broker: in `mosquitto.conf` set `listener 1883` and `allow_anonymous true`, then run `mosquitto -c mosquitto.conf -v`. Allow inbound TCP 1883 in the laptop firewall.
2. Watch: copy `src/FallDetector_trials/secrets_example.h` to `secrets.h` in the same folder and fill in the Wi-Fi name, password and the laptop's IPv4 address. `secrets.h` is not committed. Build settings are at the top of the sketch.
3. Pi: the steps below.

After a detected fall the watch waits 15 s for a BOOT cancel, then publishes the alert. It then watches for wrist movement: 60 s in a row with none publishes an escalation. A BOOT press after the alert is sent tells the dashboard the wearer is OK. Message formats are in `src/FallDetector_trials/README.md`.

## Pi

Set `BROKER` in `screen.py` to the laptop's IPv4 address, then on the Pi:

    pip install paho-mqtt
    python3 screen.py

Every fall, escalate and cancel message is appended to `alerts_log.csv` next to the script,
with the Pi's receive time and `delay_s` (receive time minus detection time on the watch).
`delay_s` is only valid when the watch and the Pi both have NTP time.

`screen.py` acknowledges every alert on `pi/dashboard/ack`; the watch keeps resending a
message until that ack arrives. A resend of a message already received is acknowledged
again but not shown or logged twice. The log's `try` column says which send got through.

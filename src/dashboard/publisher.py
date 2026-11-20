"""Test publisher for the laptop: sends a JSON message to the broker once per second."""
import json
import time

import paho.mqtt.client as mqtt

BROKER = "localhost"
TOPIC = "pi/dashboard/message"

try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
except AttributeError:  # older paho 1.x
    client = mqtt.Client()

# client.username_pw_set("pi_user", "yourpassword")  # once you enable auth
client.connect(BROKER, 1883)
client.loop_start()

n = 0
try:
    while True:
        n += 1
        payload = {
            "msg": "Hello from the laptop!",
            "n": n,
            "ts": time.strftime("%H:%M:%S"),
        }
        client.publish(TOPIC, json.dumps(payload), qos=0, retain=True)
        print("sent", payload)
        time.sleep(1)
except KeyboardInterrupt:
    client.loop_stop()
    client.disconnect()

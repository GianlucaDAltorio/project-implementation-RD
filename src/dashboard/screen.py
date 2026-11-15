"""
Pi dashboard. Subscribes to pi/dashboard/# on the laptop's broker and shows the latest
alert from the watch: red for a fall, flashing red for an escalation, green when the
wearer cancels. Every alert is also appended to alerts_log.csv with the delay since
detection (needs NTP time on the watch and the Pi).

Run: python3 screen.py    (q quits, c clears the screen)
"""
import csv
import curses
import json
import os
import threading
import time

import paho.mqtt.client as mqtt

BROKER = "172.20.10.2"  # laptop's IPv4 address
TOPIC = "pi/dashboard/#"
LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "alerts_log.csv")
LOG_FIELDS = ["recv_time", "event", "aid", "n", "msg", "p", "peak_g", "still_s",
              "detect_ms", "sent_ms", "recv_ms", "delay_s", "wait_ms", "rssi"]

state = {"event": None, "msg": "waiting for data...", "aid": "-", "ts": "-", "recv": "-",
         "delay": None, "watch": "unknown", "link": "connecting..."}
lock = threading.Lock()


def log_alert(data, recv_ms):
    delay = None
    if data.get("detect_ms"):
        delay = round((recv_ms - data["detect_ms"]) / 1000, 2)
    row = {k: data.get(k, "") for k in LOG_FIELDS}
    row.update(recv_time=time.strftime("%Y-%m-%d %H:%M:%S"), recv_ms=recv_ms,
               delay_s="" if delay is None else delay)
    new = not os.path.exists(LOG_FILE)
    with open(LOG_FILE, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=LOG_FIELDS)
        if new:
            w.writeheader()
        w.writerow(row)
    return delay


def on_connect(client, userdata, flags, rc, *extra):
    client.subscribe(TOPIC)
    with lock:
        state["link"] = "connected"


def on_disconnect(client, userdata, *args):
    with lock:
        state["link"] = "disconnected (retrying)"


def on_message(client, userdata, m):
    recv_ms = int(time.time() * 1000)
    try:
        data = json.loads(m.payload)
    except json.JSONDecodeError:
        data = {"msg": m.payload.decode(errors="replace")}

    if m.topic.endswith("/status"):
        with lock:
            state["watch"] = data.get("msg", "?")
        return

    event = data.get("event")
    is_alert = event in ("fall", "escalate", "cancel")
    delay = log_alert(data, recv_ms) if is_alert else None
    with lock:
        if not is_alert and state["event"] in ("fall", "escalate"):
            return  # test messages never hide an open alert
        state.update(event=event, msg=data.get("msg", ""), aid=data.get("aid", "-"),
                     ts=data.get("ts", "-"), recv=time.strftime("%H:%M:%S"), delay=delay)


try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
except AttributeError:  # older paho 1.x
    client = mqtt.Client()

# client.username_pw_set("pi_user", "yourpassword")  # once you enable auth
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message
client.connect_async(BROKER, 1883, keepalive=30)  # reconnects on its own
client.loop_start()


def put(win, y, text, attr=0):
    h, w = win.getmaxyx()
    if 0 <= y < h:
        x = max(0, (w - len(text)) // 2)
        try:
            win.addstr(y, x, text[: w - 1], attr)
        except curses.error:
            pass


def main(win):
    curses.curs_set(0)
    win.keypad(True)
    win.timeout(250)  # redraw 4 times a second
    red = green = 0
    if curses.has_colors():
        curses.start_color()
        curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_RED)
        curses.init_pair(2, curses.COLOR_BLACK, curses.COLOR_GREEN)
        red, green = curses.color_pair(1), curses.color_pair(2)
    tick = 0
    while True:
        tick += 1
        with lock:
            s = dict(state)
        win.erase()
        h, _ = win.getmaxyx()
        mid = h // 2

        attr = curses.A_BOLD
        if s["event"] == "fall":
            attr |= red
        elif s["event"] == "escalate":
            attr |= red | (curses.A_REVERSE if tick % 4 < 2 else 0)  # flashes about once a second
        elif s["event"] == "cancel":
            attr |= green

        put(win, 0, "PI DASHBOARD", curses.A_REVERSE)
        put(win, mid - 2, f"  {s['msg']}  ", attr)
        if s["event"]:
            put(win, mid, f"fall {s['aid']}   watch time {s['ts']}   received {s['recv']}")
            if s["delay"] is not None:
                put(win, mid + 1, f"received {s['delay']:.1f} s after detection")
        put(win, h - 2, f"watch: {s['watch']}")
        put(win, h - 1, f"link: {s['link']}   (c clear, q quit)")
        win.refresh()

        key = win.getch()
        if key in (ord("q"), 27):
            break
        if key == ord("c"):
            with lock:
                state.update(event=None, msg="no active alert", aid="-", ts="-", recv="-", delay=None)


try:
    curses.wrapper(main)
finally:
    client.loop_stop()
    client.disconnect()

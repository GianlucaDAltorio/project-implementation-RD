# WaistBoard

Waist sensor board for sensor fusion: ESP32 (classic, e.g. a DevKit) with an MPU6050 and a
BMP280 on a breadboard.

- `WaistBoard.ino`: sampling, 4 s ring buffer, Bluetooth LE server
- `fd_features.c`, `fd_features.h`: identical copies of the watch's files; `src/fall_model/fusion_parity.py` checks that they still match

## Wiring

| ESP32 | MPU6050 | BMP280 |
|---|---|---|
| 3V3 | VCC | VCC |
| GND | GND, AD0 | GND, SDO |
| GPIO 21 | SDA | SDA |
| GPIO 22 | SCL | SCL |
| 3V3 | | CSB |

Board: ESP32 Dev Module. No extra libraries. Check the wiring first with `src/waist_sensor_test`.

## Use

1. Flash the board, open the Serial Monitor at 115200 baud and keep the board still for the first
   3 s while it measures the gyro offset. It repeats the measurement if the board moved.
2. Strap it firmly to the waist. Its orientation does not matter: the features use magnitudes
   and the angle between gravity before and after the impact.
3. Power on the watch. It finds the board by its Bluetooth service and shows `WB:ok`.

Settings match FallAllD's waist logger: 238 Hz, accelerometer +-8 g, gyroscope +-2000 deg/s.
The barometer is read about 25 times a second.

## How a request works

The watch writes `<id>,<age_ms>` to the request characteristic, where `age_ms` is how long ago
the wrist impact peak was. The board looks around that moment for its own acceleration peak
(within 0.25 s, as in training, so Bluetooth delay does not matter), waits until it has 2 s of
data after it, and sets the features characteristic to `<id>,<status>,<15 values>`: the 14
window features and the height change in metres. Status 1 means the window was no longer in
the buffer. The Serial Monitor prints each request with its peak, tilt and height change.

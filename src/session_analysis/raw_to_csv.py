"""Convert a raw_nnn.bin from the watch into a CSV (units: g and deg/s).
usage: python3 raw_to_csv.py raw_001.bin [out.csv]
Each record is 16 bytes: uint32 t_ms, then int16 ax ay az gx gy gz.
Scales (must match the sketch): accel 8 g / 32768 per count, gyro 1024 dps / 32768 per count."""
import sys, struct
from pathlib import Path
ACC, GYR = 8.0 / 32768, 1024.0 / 32768
src = Path(sys.argv[1]); out = Path(sys.argv[2]) if len(sys.argv) > 2 else src.with_suffix(".csv")
data = src.read_bytes(); n = len(data) // 16
with out.open("w") as f:
    f.write("t_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps\n")
    for i in range(n):
        t, ax, ay, az, gx, gy, gz = struct.unpack_from("<I6h", data, i * 16)
        f.write(f"{t},{ax*ACC:.5f},{ay*ACC:.5f},{az*ACC:.5f},{gx*GYR:.3f},{gy*GYR:.3f},{gz*GYR:.3f}\n")
print(f"{n} samples ({n/238:.0f} s at 238 Hz) -> {out}" + ("  (last partial record ignored)" if len(data) % 16 else ""))

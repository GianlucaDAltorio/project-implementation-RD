"""Convert FallAllD .dat files to labelled CSV files.

Each trial is merged from its _A, _G, _M and _B files onto the 238 Hz accelerometer
timeline (magnetometer and barometer samples are held between updates) and written as
<trial>.csv. An index.csv with one row per trial is written alongside.

Scaling follows the LSM9DS1 settings used by the dataset authors:
accelerometer 0.244 mg/LSB (+-8 g), gyroscope 70 mdps/LSB (+-2000 dps),
magnetometer 0.14 mgauss/LSB (+-4 gauss).

Usage:
    python convert_fallalld.py <input_dir> <output_dir> <ActivityID2Str.m>
"""
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ACC_FS, MAG_FS, BAR_FS = 238, 80, 10
DEV = {1: "Neck", 2: "Wrist", 3: "Waist"}
pat = re.compile(r"S(\d+)_D(\d)_A(\d+)_T(\d+)_A\.dat$")


def activity_map(mfile):
    txt = Path(mfile).read_text()
    return {int(n): t.strip() for n, t in
            re.findall(r"case\s+(\d+)\s*\n\s*res=\s*\[res\s+'([^']*)'\]", txt)}


def load(p, cols):
    return pd.read_csv(p, header=None, names=cols)


src, dst = Path(sys.argv[1]), Path(sys.argv[2])
dst.mkdir(parents=True, exist_ok=True)
ACT = activity_map(sys.argv[3])
assert len(ACT) == 79, len(ACT)   # 44 daily activities + 35 falls

rows = []
for fa in sorted(src.rglob("*_A.dat")):
    m = pat.search(fa.name)
    if not m:
        continue
    s, d, a, t = map(int, m.groups())
    base = fa.name[:-6]
    parts = {k: fa.with_name(f"{base}_{k}.dat") for k in "GMB"}
    if not all(p.exists() for p in parts.values()):
        print("missing sensor file for", base)
        continue
    acc = load(fa, ["acc_x_g", "acc_y_g", "acc_z_g"]) * 0.000244
    gyr = load(parts["G"], ["gyr_x_dps", "gyr_y_dps", "gyr_z_dps"]) * 0.07
    mag = load(parts["M"], ["mag_x_gauss", "mag_y_gauss", "mag_z_gauss"]) * 0.00014
    bar = load(parts["B"], ["pressure_hPa", "temp_C"])
    n = min(len(acc), len(gyr))
    df = pd.concat([acc.iloc[:n], gyr.iloc[:n]], axis=1)
    df.insert(0, "t_s", np.arange(n) / ACC_FS)
    for frame, fs in ((mag, MAG_FS), (bar, BAR_FS)):
        idx = np.minimum((df["t_s"] * fs).astype(int), len(frame) - 1)
        for c in frame.columns:
            df[c] = frame[c].to_numpy()[idx]
    out = dst / f"{base}.csv"
    df.round(6).to_csv(out, index=False)
    rows.append(dict(file=out.name, subject=s, device=DEV.get(d, d), activity_id=a,
                     activity=ACT.get(a, "Unknown activity"), is_fall=int(a >= 100), trial=t,
                     n_acc=len(acc), n_gyr=len(gyr), n_mag=len(mag), n_bar=len(bar),
                     duration_s=round(n / ACC_FS, 2)))

pd.DataFrame(rows).to_csv(dst / "index.csv", index=False)
print(len(rows), "trials written to", dst)

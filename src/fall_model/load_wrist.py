"""Loader for the wrist recordings of the selected subset.

load() returns the wrist rows of features.csv together with the scaled accelerometer (g)
and gyroscope (deg/s) arrays of each trial. FS is the dataset sampling rate in Hz.
"""
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
FS = 238
SUBSET = ROOT / "data" / "fallalld" / "my_subset"
FEATURES = ROOT / "data" / "features"


def load(src=SUBSET, features=FEATURES / "features.csv"):
    meta = pd.read_csv(features)
    meta = meta[meta.device == "Wrist"].reset_index(drop=True)
    acc, gyr = [], []
    for b in meta.file:
        acc.append(np.loadtxt(Path(src) / f"{b}_A.dat", delimiter=",") * 0.000244)
        gyr.append(np.loadtxt(Path(src) / f"{b}_G.dat", delimiter=",") * 0.07)
    return meta, acc, gyr

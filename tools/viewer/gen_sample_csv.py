#!/usr/bin/env python3
"""Generate synthetic KDatalogger logs for developing and testing the viewer.

The files mimic what logger_service.c writes, byte for byte: UTF-8 BOM, ';'
separator, ',' decimal mark, CRLF, "Data;Ora;Tempo [s]" then one column per
channel headed "<name> [<unit>]" (names from data_model_channels.c), empty
cells for invalid readings. Values are invented but shaped like a tractor
pull: exhaust temperatures and boost ramp up during the run and decay after.

Deterministic (fixed seed): re-running produces identical files.

Usage: python3 tools/viewer/gen_sample_csv.py [output_dir]
       (default: tools/viewer/samples)
"""

import math
import random
import sys
from pathlib import Path

TC_CHANNELS = ["Cil 1", "Cil 2", "Cil 3", "Cil 4", "IC in", "IC out", "Olio", "Acqua"]
AI_CHANNELS = ["P IC in", "P IC out", "P scar", "P benz", "P olio"]


def smoothstep(t0, t1, t):
    """0 before t0, 1 after t1, smooth in between."""
    if t <= t0:
        return 0.0
    if t >= t1:
        return 1.0
    x = (t - t0) / (t1 - t0)
    return x * x * (3.0 - 2.0 * x)


def pull_load(t, start, length, rise=3.0):
    """Engine load 0..1: ramps up at start, holds, drops at the end."""
    return smoothstep(start, start + rise, t) * (1.0 - smoothstep(start + length, start + length + 1.0, t))


def thermal(t, start, length, tau_up, tau_down):
    """First-order thermal response to a load step from start to start+length."""
    if t < start:
        return 0.0
    end = start + length
    if t <= end:
        return 1.0 - math.exp(-(t - start) / tau_up)
    peak = 1.0 - math.exp(-length / tau_up)
    return peak * math.exp(-(t - end) / tau_down)


def quantize_tc(value):
    """MAX31855 resolution is 0.25 degC."""
    return round(value * 4.0) / 4.0


def fmt(value, decimals):
    return f"{value:.{decimals}f}".replace(".", ",")


def write_log(path, *, start_clock, duration_s, period_s, pull_start, pull_len, heat,
              missing=(), dropouts=(), jitter=True, seed=1):
    rng = random.Random(seed)
    header = ["Data", "Ora", "Tempo [s]"]
    header += [f"{name} [°C]" for name in TC_CHANNELS]
    header += [f"{name} [V]" for name in AI_CHANNELS]

    rows = []
    t = 0.0
    cyl_offsets = [0.0, 25.0, -18.0, 12.0]
    while t <= duration_s + 1e-9:
        load = pull_load(t, pull_start, pull_len)
        egt = thermal(t, pull_start, pull_len, tau_up=2.5, tau_down=14.0)
        air = thermal(t, pull_start, pull_len, tau_up=4.0, tau_down=20.0)

        tc = []
        for index, offset in enumerate(cyl_offsets):
            tc.append(240.0 + offset * 0.4 + (480.0 + offset) * heat * egt + rng.gauss(0, 2.0))
        tc.append(38.0 + 150.0 * heat * air + rng.gauss(0, 0.6))    # IC in
        tc.append(31.0 + 42.0 * heat * air + rng.gauss(0, 0.4))     # IC out
        tc.append(86.0 + 0.08 * t + 5.0 * air + rng.gauss(0, 0.2))  # Olio
        tc.append(82.0 + 0.05 * t + 3.0 * air + rng.gauss(0, 0.2))  # Acqua

        ai = [
            0.52 + 3.30 * heat * load + rng.gauss(0, 0.012),        # P IC in (boost)
            0.51 + 3.05 * heat * load + rng.gauss(0, 0.012),        # P IC out
            0.60 + 2.60 * heat * load + rng.gauss(0, 0.015),        # P scar
            2.48 - 0.35 * load + rng.gauss(0, 0.010),               # P benz
            1.45 + 1.55 * load + rng.gauss(0, 0.010),               # P olio
        ]

        cells = []
        if start_clock is None:
            cells += ["", ""]
        else:
            day, clock_s = start_clock
            now = clock_s + int(t)
            cells += [day, f"{now // 3600 % 24:02d}:{now // 60 % 60:02d}:{now % 60:02d}"]

        elapsed_cs = int(round(t * 100.0))
        cells.append(f"{elapsed_cs // 100},{elapsed_cs % 100:02d}")

        for index, value in enumerate(tc):
            name = TC_CHANNELS[index]
            dropped = any(ch == name and a <= t < b for ch, a, b in dropouts)
            cells.append("" if name in missing or dropped else fmt(quantize_tc(value), 2))
        for index, value in enumerate(ai):
            cells.append("" if AI_CHANNELS[index] in missing else fmt(value, 3))
        rows.append(";".join(cells))

        step = period_s
        if jitter and rng.random() < 0.08:
            step += 0.02  # the ~20 ms flash-erase jitter seen on the real device
        t = round(t + step, 2)

    with open(path, "w", encoding="utf-8", newline="") as out:
        out.write("﻿" + ";".join(header) + "\r\n")
        for row in rows:
            out.write(row + "\r\n")
    print(f"{path}: {len(rows)} righe")


def main():
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "samples"
    out_dir.mkdir(parents=True, exist_ok=True)

    # Two pulls on the same afternoon, 100 ms period. Water probe unplugged in
    # the first, a loose oil probe in the second (short gap).
    write_log(out_dir / "log_0101.csv", start_clock=("26/09/2026", 14 * 3600 + 32 * 60 + 5),
              duration_s=60.0, period_s=0.1, pull_start=15.0, pull_len=12.0, heat=0.92,
              missing=("Acqua",), seed=101)
    write_log(out_dir / "log_0102.csv", start_clock=("26/09/2026", 15 * 3600 + 4 * 60 + 41),
              duration_s=55.0, period_s=0.1, pull_start=10.0, pull_len=14.0, heat=1.0,
              dropouts=(("Olio", 31.0, 33.5),), seed=102)
    # A 10-minute warm-up in the pits at 1 s, clock never set (empty Data/Ora).
    write_log(out_dir / "log_0100.csv", start_clock=None, duration_s=600.0, period_s=1.0,
              pull_start=420.0, pull_len=20.0, heat=0.55, jitter=False, seed=100)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Level-match a user.m3p using a calibration table from the harness (render_test ... userpresets with CAL_OUT=cal.tsv).
Each line of cal.tsv: preset index (0-based within the file), peak, rms measured at the file's current Volume.
Sets Volume so rms is about 0.07 with peak at most 0.5 (same rule as the factory bank), clamped to -40..+6 dB.
usage: level_match.py USER.m3p CAL.tsv OUT.m3p"""
import math, sys
src, cal, out = sys.argv[1:4]
lines = [l.rstrip("\n") for l in open(src) if "|" in l]
res = {int(a): (float(b), float(c)) for a, b, c in (l.split("\t") for l in open(cal))}
assert len(res) == len(lines), (len(res), len(lines))
with open(out, "w") as fh:
    for i, l in enumerate(lines):
        name, vals = l.split("|"); v = vals.split(); pk, rms = res[i]
        d = min(20 * math.log10(0.07 / max(rms, 1e-6)), 20 * math.log10(0.5 / max(pk, 1e-6)))
        v[0] = "%.1f" % max(-40.0, min(6.0, float(v[0]) + d))
        fh.write(name + "|" + " ".join(v) + "\n")
print(len(lines), "presets level-matched ->", out)

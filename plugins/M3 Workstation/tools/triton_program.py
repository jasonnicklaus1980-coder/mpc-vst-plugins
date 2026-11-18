#!/usr/bin/env python3
"""Decode Korg Triton-family 540-byte PCM programs (PBK1 banks in .PCG files).
Layout from Korg's TRITON MIDI Implementation, TABLE 1 (PROGRAM PARAMETERS for PCM Synth).
usage: triton_program.py FILE.PCG [--json OUT.json]   (reads only; no audio is extracted)"""
import json, struct, sys

def s8(b): return b - 256 if b > 127 else b
def eg(p, o, n_amp=False):
    f = ["start", "attack_time", "attack_level", "decay_time", "break_level", "slope_time", "sustain_level", "release_time"]
    d = {k: (p[o + i] if (n_amp or k.endswith("time")) else s8(p[o + i])) for i, k in enumerate(f)}
    if not n_amp: d["release_level"] = s8(p[o + 8])
    return d

def osc(p, base):
    """base = 230 for oscillator 1, 384 for oscillator 2 (osc 2 is a copy, 154 bytes later)."""
    g = lambda o: p[base + o - 230]
    def sample(o): return ((g(o) & 0x7F) << 8) | g(o + 1)
    return {
        "hi": {"sample": sample(230), "bank": g(232), "level": g(233), "reverse": bool(g(230) & 0x40), "start_offset": bool(g(230) & 0x80)},
        "low": {"sample": sample(234), "bank": g(236), "level": g(237), "reverse": bool(g(234) & 0x40), "start_offset": bool(g(234) & 0x80)},
        "delay_start": g(238), "vel_sample_sw": g(239), "vel_zone": [g(240), g(241)],
        "lfo1": {"wave": g(242) & 0x1F, "key_sync": bool(g(242) & 0x80), "freq": g(243), "offset": s8(g(244)), "delay": g(245), "fade": g(246)},
        "lfo2": {"wave": g(252) & 0x1F, "key_sync": bool(g(252) & 0x80), "freq": g(253), "offset": s8(g(254)), "delay": g(255), "fade": g(256)},
        "pitch": {"octave": s8(g(262)), "transpose": s8(g(263)), "tune_cents": struct.unpack(">h", bytes([g(264), g(265)]))[0],
                  "slope": s8(g(268)), "eg_int": s8(g(269)), "lfo1_int": s8(g(272)), "lfo2_int": s8(g(273)),
                  "portamento": bool(g(274) & 1), "porta_time": g(275)},
        "filter": {"type": g(286), "trim": g(287), "resonance": g(288),
                   "A": {"cutoff": g(294), "kbd_track": s8(g(295)), "eg_int": s8(g(300)), "eg_vel": s8(g(301)), "lfo1": s8(g(302)), "lfo2": s8(g(303))},
                   "B": {"cutoff": g(309), "kbd_track": s8(g(310)), "eg_int": s8(g(315)), "eg_vel": s8(g(316))},
                   "eg": {k: v for k, v in zip(["start", "attack_time", "attack_level", "decay_time", "break_level", "slope_time", "sustain_level", "release_time", "release_level"],
                          [s8(g(324)), g(325), s8(g(326)), g(327), s8(g(328)), g(329), s8(g(330)), g(331), s8(g(332))])},
                   "kbd_track_keys": [g(342), s8(g(343)), g(344), s8(g(345))]},
        "amp": {"level": g(346), "vel_int": s8(g(347)),
                "eg": {k: v for k, v in zip(["start", "attack_time", "attack_level", "decay_time", "break_level", "slope_time", "sustain_level", "release_time"], [g(i) for i in range(356, 364)])},
                "kbd_track_keys": [g(374), s8(g(375)), g(376), s8(g(377))]},
        "output": {"pan": g(379) & 0x7F, "send1": g(382), "send2": g(383)},
    }

def program(p):
    assert len(p) == 540
    return {
        "name": p[0:16].decode("latin1").rstrip("\0 "),
        "category": p[206], "osc_mode": ["single", "double", "drums"][p[204] & 3] if (p[204] & 3) < 3 else "?",
        "mono": bool(p[204] & 4), "legato": bool(p[204] & 8), "hold": bool(p[204] & 0x80),
        "ifx_types": [p[32], p[56], p[80], p[104], p[128]], "ifx_on": [bool(p[33] & 0x40)],
        "mfx_types": [p[152], p[172]], "mfx_on": [bool(p[153] & 0x40), bool(p[173] & 0x40)],
        "arp": {"tempo": p[192], "on": bool(p[193]), "pattern": p[194], "octave": (p[195] & 3) + 1, "resolution": (p[195] >> 2) & 7,
                "gate": p[196], "velocity": p[197], "swing": s8(p[198])},
        "pitch_eg": eg(p, 216),
        "osc1": osc(p, 230), "osc2": osc(p, 384),
    }

def read_programs(path):
    d = open(path, "rb").read()
    out = []; i = 0
    while True:
        i = d.find(b"PBK1", i)
        if i < 0: break
        cnt, rec = struct.unpack(">II", d[i + 8:i + 16])
        if rec == 540 and 0 < cnt <= 256 and i + 20 + cnt * 540 <= len(d):
            out += [program(d[i + 20 + k * 540:i + 20 + (k + 1) * 540]) for k in range(cnt)]
            i += 20 + cnt * 540
        else:
            i += 4
    return out

if __name__ == "__main__":
    progs = read_programs(sys.argv[1])
    print(len(progs), "programs"); [print("  ", p["name"], p["osc_mode"]) for p in progs[:5]]
    if "--json" in sys.argv: json.dump(progs, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=1)

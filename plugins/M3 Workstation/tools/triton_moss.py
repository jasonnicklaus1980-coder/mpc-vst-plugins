#!/usr/bin/env python3
"""Decode Korg Triton EXB-MOSS programs (604-byte records in MBK1 banks).
Layout: Korg TRITON MIDI Implementation, TABLE 2-1. Reads parameters only.
usage: triton_moss.py FILE.PCG [--json OUT.json]"""
import json, struct, sys
s8 = lambda b: b - 256 if b > 127 else b
OSC = ["Standard", "Comb Filter", "VPM", "Resonance", "Ring Mod", "Cross Mod", "Sync Mod", "Organ", "E.Piano", "Brass", "Reed", "Plucked String", "Bowed String"]
def eg(p, o): return dict(zip(["start", "attack_time", "attack_level", "decay_time", "break_level", "slope_time", "sustain_level", "release_time", "release_level"],
                              [s8(p[o]), p[o+1], s8(p[o+2]), p[o+3], s8(p[o+4]), p[o+5], s8(p[o+6]), p[o+7], s8(p[o+8])]))
# Stored filter type and EG selector are 1-based in real files (1=LPF..5=DualBP; 1..4=EG1..4, 5=Amp EG): checked on 256
# programs, where 0 never occurs. Korg's Triton table lists them 0-based; the Trinity V3 table gives 1~5.
def filt(p, o): return {"type": ["LPF", "HPF", "BPF", "BRF", "DualBP"][p[o] - 1] if 1 <= p[o] <= 5 else "?", "trim": p[o+1], "cutoff": p[o+2],
                        "kbd": [p[o+3], p[o+4], s8(p[o+5]), s8(p[o+6])], "eg": p[o+7], "eg_int": s8(p[o+8]), "resonance": p[o+13]}
def program(p):
    assert len(p) == 604
    mix = {n: (p[475 + 6*i], p[478 + 6*i]) for i, n in enumerate(["osc1", "osc2", "sub", "noise", "feedback"])}
    return {"name": p[:16].decode("latin1").rstrip("\0 "), "category": p[206], "voice_assign": ["mono_multi", "mono_single", "poly"][min((p[204] >> 2) & 3, 2)],
            "unison": ["off", "2", "3", "6"][p[218] & 3], "unison_detune": p[219], "arp": {"tempo": p[192], "on": bool(p[193])},
            "egs": [eg(p, 220 + 19 * i) for i in range(4)], "lfo1": {"wave": p[296] & 0x3F, "freq": p[297]},
            "osc1": {"std_wave": ["saw", "pulse"][p[363] & 1], "type": OSC[p[349]] if p[349] < 13 else "?", "octave": p[350] - 2, "transpose": s8(p[351]), "tune": s8(p[352])},
            "osc2": {"type": OSC[p[401]] if p[401] < 9 else "?", "octave": p[402] - 2, "transpose": s8(p[403]), "tune": s8(p[404])},
            "sub": {"octave": p[453] - 2, "wave": ["saw", "square", "triangle", "sine"][p[466] & 3]},
            "mixer": mix, "noise": {"type": ["thru", "lpf", "hpf", "bpf"][p[467] & 3], "cutoff": p[469]},
            "filter1": filt(p, 507), "filter2": filt(p, 534),
            "amp": {"level": p[561], "eg": dict(zip(["attack_time", "attack_level", "decay_time", "break_level", "slope_time", "sustain_level", "release_time"], p[580:587]))},
            "routing": ["serial1", "serial2", "parallel"][min(p[506] & 3, 2)], "amp2_level": p[570],
            "output": {"pan": p[598], "level": p[601], "send1": p[602], "send2": p[603]},
            "_fixed_ok": (p[204] & 3) == 3 and p[567] == 99 and p[579] == 0 and p[587] == 0}
def read_programs(path):
    d = open(path, "rb").read(); out = []; i = 0
    while True:
        i = d.find(b"MBK1", i)
        if i < 0: break
        cnt, rec = struct.unpack(">II", d[i+8:i+16])
        # Only a real bank: 604-byte records that fit in the file. (Divided-bank PCGs also carry MBK1 name entries.)
        if rec == 604 and 0 < cnt <= 256 and i + 20 + cnt * 604 <= len(d):
            out += [program(d[i+20+k*604:i+20+(k+1)*604]) for k in range(cnt)]
            i += 20 + cnt * 604
        else:
            i += 4
    return out
if __name__ == "__main__":
    ps = read_programs(sys.argv[1]); print(len(ps), "MOSS programs"); [print("  ", p["name"], p["osc1"]["type"], p["filter1"]["type"]) for p in ps[:5]]
    if "--json" in sys.argv: json.dump(ps, open(sys.argv[sys.argv.index("--json")+1], "w"), indent=1)

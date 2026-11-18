#!/usr/bin/env python3
"""Convert decoded Korg Triton programs into APPROXIMATE M3 Workstation presets.
Writes Content/Presets/user.m3p (one preset per line: name|41 values) which the plugin loads at start-up.

What transfers: amp envelope, filter cutoff/resonance/type/envelope amount/key tracking, LFO shape/rate/destination,
oscillator-2 interval/detune (Double programs), arpeggiator on/off and direction, drum-mode flag.
What does NOT: the sound itself. Triton PCM programs point at samples in the Triton's ROM/RAM (not in the PCG), so the
preset uses the plugin's own oscillators. Effects, modulation routings and tempo-sync are not mapped. Treat every
converted preset as a starting point, not a copy.
usage: triton_to_m3.py OUT.m3p FILE.PCG [FILE.PCG ...]   (max 512 programs per output)"""
import math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import triton_program as TP, triton_moss as TM
ORDER = ["vol","osc1","osc2","semi","detune","mix","cutoff","reso","fenv","atk","dec","sus","rel","source","inst","loop","ftype","ktrack",
         "lfoshape","lforate","lfodepth","lfodest","lfoon","chorus","dlymix","dlytime","dlyfb","revmix","revsize","arpon","arpmode","arprate",
         "arpgate","arpoct","arpswing","arplatch","drumtune","drumdecay","drumkit","mode","combi"]
D = dict(vol=-8, osc1=2, osc2=2, semi=0, detune=7, mix=0, cutoff=3000, reso=20, fenv=0, atk=5, dec=250, sus=70, rel=300, source=0, inst=1, loop=1,
         ftype=0, ktrack=0, lfoshape=0, lforate=5, lfodepth=0, lfodest=0, lfoon=0, chorus=0, dlymix=0, dlytime=350, dlyfb=35, revmix=0, revsize=50,
         arpon=0, arpmode=0, arprate=3, arpgate=80, arpoct=1, arpswing=0, arplatch=0, drumtune=0, drumdecay=100, drumkit=0, mode=0, combi=0)
clamp = lambda v, lo, hi: max(lo, min(hi, v))
ms = lambda t: clamp(2.0 * (3000.0 ** (t / 99.0)), 1, 6000)          # EG time 0..99 -> ~2 ms .. 6 s (approximate curve)
hz = lambda c: clamp(20.0 * (1000.0 ** (c / 99.0)), 20, 18000)       # cutoff 0..99 -> 20 Hz .. 20 kHz (approximate)
# Triton LFO wave ids -> plugin shape (Sine/Tri/Square/S&H)
LFO_TRITON = {0: 1, 1: 1, 2: 3, 3: 1, 4: 1, 5: 2, 6: 0, 7: 0, 8: 1, 15: 3, 16: 3, 17: 3}
LFO_MOSS = {0: 1, 1: 1, 2: 3, 3: 0, 4: 1, 5: 1, 6: 1, 7: 1, 8: 2, 9: 3, 10: 3}

def lfo_part(shape_map, wave, freq, fmax, pitch_i, filt_i, amp_i):
    v = dict(zip(("pitch", "cutoff", "amp"), (abs(pitch_i), abs(filt_i), abs(amp_i))))
    dest = max(v, key=v.get); depth = v[dest]
    if depth < 5: return {}
    return dict(lfoon=1, lfoshape=shape_map.get(wave, 0), lforate=round(clamp(0.1 * (200.0 ** (freq / fmax)), 0.1, 20), 2),
                lfodepth=clamp(depth, 0, 100), lfodest=["pitch", "cutoff", "amp"].index(dest))

def from_pcm(p):
    o = p["osc1"]; v = dict(D); name = p["name"][:23]
    if p["osc_mode"] == "drums": v.update(source=2, atk=1, sus=100, vol=-4); return name, v
    ae = o["amp"]["eg"]; fe = o["filter"]["eg"]; fa = o["filter"]["A"]
    v.update(atk=ms(ae["attack_time"]), dec=ms(ae["decay_time"]), sus=clamp(ae["sustain_level"], 0, 100), rel=ms(ae["release_time"]),
             cutoff=max(150, hz(fa["cutoff"])), reso=clamp(o["filter"]["resonance"], 0, 100), fenv=clamp(fa["eg_int"], -100, 100),
             ktrack=clamp(abs(fa["kbd_track"]), 0, 100), ftype=0)
    v.update(lfo_part(LFO_TRITON, o["lfo1"]["wave"], o["lfo1"]["freq"], 99, o["pitch"]["lfo1_int"], fa["lfo1"], 0))
    if p["osc_mode"] == "double":
        o2 = p["osc2"]; v.update(mix=50, semi=clamp(12 * (o2["pitch"]["octave"] - o["pitch"]["octave"]) + o2["pitch"]["transpose"] - o["pitch"]["transpose"], -24, 24),
                                 detune=clamp(o2["pitch"]["tune_cents"] - o["pitch"]["tune_cents"], -50, 50))
    a = p["arp"]
    if a["on"]: v.update(arpon=1, arpmode=[0, 1, 2, 3][a["pattern"] % 4], arpoct=clamp(a["octave"], 1, 4), arpgate=clamp(a["gate"] if a["gate"] <= 100 else 80, 5, 100),
                         arprate=[4, 3, 2, 1, 0, 0][min(a["resolution"], 5)] if a["resolution"] <= 5 else 3)
    if v["arpon"]: v["atk"] = min(v["atk"], 40)   # an arp step is short; a slow attack would never open
    return name, v

def from_moss(p):
    v = dict(D); name = p["name"][:23]; ae = p["amp"]["eg"]
    # Parallel routing: mixer 1 feeds filter 1 and mixer 2 feeds filter 2. Use the filter that carries more signal.
    m1 = sum(a for a, b in p["mixer"].values()); m2 = sum(b for a, b in p["mixer"].values())
    f = p["filter2"] if (p["routing"] == "parallel" and m2 > m1) else p["filter1"]
    v.update(atk=ms(ae["attack_time"]), dec=ms(ae["decay_time"]), sus=clamp(ae["sustain_level"], 0, 100), rel=ms(ae["release_time"]),
             cutoff=max(150, hz(f["cutoff"])), reso=clamp(f["resonance"], 0, 100), ftype={"LPF": 0, "HPF": 1, "BPF": 2, "BRF": 3, "DualBP": 2}.get(f["type"], 0),
             ktrack=clamp(abs(f["kbd"][2]), 0, 100), fenv=clamp(f["eg_int"], -100, 100))
    o1, o2 = p["osc1"], p["osc2"]
    v["osc1"] = 3 if o1["std_wave"] == "pulse" and o1["type"] == "Standard" else 2
    if p["mixer"]["osc2"][0] > 10: v.update(mix=50, semi=clamp(12 * (o2["octave"] - o1["octave"]) + o2["transpose"] - o1["transpose"], -24, 24), detune=clamp(o2["tune"] - o1["tune"], -50, 50))
    if p["unison"] != "off": v.update(mix=50, detune=clamp(p["unison_detune"] // 4, 0, 50))
    v.update(lfo_part(LFO_MOSS, p["lfo1"]["wave"], p["lfo1"]["freq"], 199, 0, 0, 0))
    if p["arp"]["on"]: v.update(arpon=1, atk=min(v["atk"], 40))
    return name, v

def convert(path):
    out = []
    for p in TP.read_programs(path): out.append(from_pcm(p))
    for p in TM.read_programs(path): out.append(from_moss(p))
    return out

def fmt(x): return ("%.4g" % x)
if __name__ == "__main__":
    out = sys.argv[1]; items = []
    for f in sys.argv[2:]: items += convert(f)
    items = items[:512]
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w") as fh:
        for name, v in items:
            assert "|" not in name and set(v) == set(D)
            fh.write(name.replace("|", "/") + "|" + " ".join(fmt(float(v[k])) for k in ORDER) + "\n")
    print(len(items), "presets written to", out)

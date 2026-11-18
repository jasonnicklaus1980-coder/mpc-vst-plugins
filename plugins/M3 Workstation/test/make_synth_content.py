# Synthetic test content: sine samples at known frequencies. No third-party audio.
import math, os, struct, sys, wave
out = sys.argv[1]
os.makedirs(out + "/Samples/sine", exist_ok=True); os.makedirs(out + "/Instruments", exist_ok=True)
def sine(path, hz, rate, secs):
    n = int(rate * secs)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(16000 * math.sin(2 * math.pi * hz * i / rate))) for i in range(n)))
sine(out + "/Samples/sine/a440_22k.wav", 440.0, 22050, 1.0)
# loop on a whole number of cycles: 440 Hz at 22050 = 50.113 frames/cycle; use 2205 frames = 100 cycles of 441 Hz-ish -> use exact: loop 0..22050 would be full length
open(out + "/Instruments/sine.map", "w").write(
    "#M3MAP 1\nname Sine\nregion Samples/sine/a440_22k.wav 0 127 69 0 0 64 1000 21000\n")
open(out + "/Instruments/zoned.map", "w").write(
    "#M3MAP 1\nname Zoned\nregion Samples/sine/a440_22k.wav 60 72 66 100 0 64 0 0\n")
open(out + "/Instruments/index.txt", "w").write("sine.map\nzoned.map\n")

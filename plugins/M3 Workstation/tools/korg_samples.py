#!/usr/bin/env python3
"""Reader for Korg sample files: .KSF (sample), .KMP (multisample keymap), .KSC (file list).

Clean implementation from the layout in Korg's published manuals; field order cross-checked
against the description in libgig (GPL, used as a reference only; no code copied).
Format: flat chunks, each = 4-byte ID + 4-byte big-endian size + data. All numbers big-endian.
  KSF: SMP1 (32 B: name16, bank1, start3, start2 4, loopStart 4, loopEnd 4)
       SMD1 (12 B header: rate4, attr1, loopTune1, channels1, bitDepth1, points4; then PCM data)
  KMP: MSP1 (name16, nSamples1, attr1), optional NAME (24-char long name),
       RLP1 (nSamples x 18 B: key1 (bit7=transpose), topKey1, tune1, level1, pan1, cutoff1, file12)
  KSC: text, first line "#KORG Script Version 1.0", then one KMP/KSF file name per line.
STATUS: tested on synthetic files and on real Korg OASYS/M3-era files (one 8-sample multisample, one 14-keymap/191-sample preload set); 148 non-empty samples converted cleanly.
Notes: placeholder sample name 'SKIPPEDSAMPL' means the set omitted that sample; 0-point KSFs are empty stubs. Attr bits 0x40/0x80 are undocumented here. Compressed samples (attr bit 0x10) are not supported.
Usage:
  python3 -I korg_samples.py info  FILE.KSF|FILE.KMP|FILE.KSC
  python3 -I korg_samples.py wav   FILE.KSF out.wav
  python3 -I korg_samples.py keymap FILE.KMP out.json        (looks for KSF files beside the KMP)
  python3 -I korg_samples.py export FILE.KMP CONTENT_DIR     (WAVs + Instruments/<slug>.map + index.txt for the M3 plugin)
"""
import json, os, struct, sys, wave


class KorgError(Exception):
    pass


def chunks(data):
    pos, out = 0, {}
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4].decode("latin-1")
        size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        if pos + 8 + size > len(data):
            raise KorgError(f"chunk {cid} at {pos} runs past end of file ({size} bytes)")
        out[cid] = data[pos + 8:pos + 8 + size]
        pos += 8 + size
    return out


def text(b):
    return b.split(b"\x00")[0].decode("latin-1").rstrip()


def read_ksf(path):
    c = chunks(open(path, "rb").read())
    if "SMP1" not in c or "SMD1" not in c:
        raise KorgError("not a KSF: missing SMP1/SMD1")
    s, d = c["SMP1"], c["SMD1"]
    if len(s) < 32 or len(d) < 12:
        raise KorgError("KSF chunks too small")
    start = int.from_bytes(s[17:20], "big")
    start2, loop_start, loop_end = struct.unpack(">III", s[20:32])
    rate, attr, tune, ch, bits, points = struct.unpack(">IBbBBI", d[:12])
    return dict(name=text(s[:16]), bank=s[16], start=start, start2=start2, loop_start=loop_start,
                loop_end=loop_end, rate=rate, attr=attr, loop_tune=tune, channels=ch, bits=bits,
                points=points, compressed=bool(attr & 0x10), empty=(points == 0), pcm=d[12:])


def read_kmp(path):
    c = chunks(open(path, "rb").read())
    if "MSP1" not in c or "RLP1" not in c:
        raise KorgError("not a KMP: missing MSP1/RLP1")
    m, r = c["MSP1"], c["RLP1"]
    if len(m) < 18:
        raise KorgError("MSP1 too small")
    n = m[16]
    if len(r) < 18 * n:
        raise KorgError("RLP1 too small for declared sample count")
    regions, prev_top = [], -1
    for i in range(n):
        b = r[i * 18:(i + 1) * 18]
        top = b[1] & 0x7F
        regions.append(dict(original_key=b[0] & 0x7F, transpose=bool(b[0] >> 7), low_key=prev_top + 1,
                            top_key=top, tune=struct.unpack("b", b[2:3])[0], level=struct.unpack("b", b[3:4])[0],
                            pan=b[4] & 0x7F, filter_cutoff=struct.unpack("b", b[5:6])[0], sample=text(b[6:18])))
        prev_top = top
    long_name = text(c["NAME"][:24]) if "NAME" in c else ""
    return dict(name=long_name or text(m[:16]), short_name=text(m[:16]), attr=m[17], regions=regions)


def read_ksc(path):
    lines = [l.strip() for l in open(path, encoding="latin-1").read().splitlines()]
    return [l for l in lines if l and not l.startswith("#") and l.upper().endswith((".KMP", ".KSF"))]


def audio_span(ksf):
    """(lo, hi) frame range of the real audio. Verified on T.R. 808 kick: KSF has `start`=2 padding
    points before the audio and the audio ends at loop_end inclusive; this reproduced the original
    WAV sample-for-sample (98381/98381)."""
    lo = ksf["start"] if 0 <= ksf["start"] < ksf["points"] else 0
    hi = ksf["loop_end"] + 1 if lo <= ksf["loop_end"] < ksf["points"] else ksf["points"]
    return lo, hi


def ksf_to_wav(ksf, out):
    if ksf["empty"]:
        raise KorgError("empty sample (0 points)")
    if ksf["compressed"]:
        raise KorgError("compressed KSF not supported")
    if ksf["bits"] not in (8, 16):
        raise KorgError(f"unsupported bit depth {ksf['bits']}")
    width = ksf["bits"] // 8
    lo, hi = audio_span(ksf)
    fw = width * ksf["channels"]
    pcm = ksf["pcm"][lo * fw:hi * fw]
    if width == 2:   # big-endian signed -> little-endian
        pcm = b"".join(pcm[i + 1:i + 2] + pcm[i:i + 1] for i in range(0, len(pcm) - 1, 2))
    else:            # assume signed 8-bit -> WAV unsigned 8-bit
        pcm = bytes((b + 128) & 0xFF for b in pcm)
    with wave.open(out, "wb") as w:
        w.setnchannels(ksf["channels"]); w.setsampwidth(width); w.setframerate(ksf["rate"]); w.writeframes(pcm)


def slug(name):
    out = "".join(c.lower() if c.isalnum() else "-" for c in name).strip("-")
    return "-".join(p for p in out.split("-") if p) or "instrument"


def export_kmp(kmp_path, content_dir):
    """Write WAVs and a plain-text keymap that the plugin loads from its Content/ folder.
    Map format:  region <wav path under Content/> <low> <top> <root> <tune> <level> <pan> <loopStart> <loopEnd>
    (loop points are sample frames in the WAV; loopEnd <= loopStart means no loop)."""
    kmp = read_kmp(kmp_path)
    base = os.path.dirname(os.path.abspath(kmp_path))
    folder = os.path.join(base, os.path.splitext(os.path.basename(kmp_path))[0])
    name = slug(kmp["name"])
    wav_dir = os.path.join(content_dir, "Samples", name)
    os.makedirs(wav_dir, exist_ok=True)
    os.makedirs(os.path.join(content_dir, "Instruments"), exist_ok=True)
    lines, skipped = ["#M3MAP 1", "name " + kmp["name"].replace("\n", " ")], []
    for r in kmp["regions"]:
        src = next((c for c in (os.path.join(base, r["sample"]), os.path.join(folder, r["sample"])) if os.path.exists(c)), None)
        if not src:
            skipped.append((r["sample"], "file not found")); continue
        k = read_ksf(src)
        if k["empty"] or k["compressed"] or k["bits"] not in (8, 16):
            skipped.append((r["sample"], "empty/compressed/unsupported")); continue
        wav = slug(os.path.splitext(r["sample"])[0]) + ".wav"
        ksf_to_wav(k, os.path.join(wav_dir, wav))
        lo, hi = audio_span(k)
        n = hi - lo
        le = k["loop_end"] - lo if lo <= k["loop_start"] < k["loop_end"] < k["points"] else 0
        ls = k["loop_start"] - lo if le else 0
        if le >= n: le = 0; ls = 0
        lines.append(f"region Samples/{name}/{wav} {r['low_key']} {r['top_key']} {r['original_key']} {r['tune']} {r['level']} {r['pan']} {ls} {le}")
    if len(lines) <= 2:
        raise KorgError("no usable samples in " + kmp_path)
    map_path = os.path.join(content_dir, "Instruments", name + ".map")
    open(map_path, "w").write("\n".join(lines) + "\n")
    idx = os.path.join(content_dir, "Instruments", "index.txt")
    existing = open(idx).read().split() if os.path.exists(idx) else []
    if name + ".map" not in existing:
        open(idx, "w").write("\n".join(existing + [name + ".map"]) + "\n")
    return map_path, skipped


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    cmd, path = argv[1], argv[2]
    if cmd == "info":
        ext = path.lower().rsplit(".", 1)[-1]
        if ext == "ksf":
            k = read_ksf(path); k.pop("pcm"); print(json.dumps(k, indent=1))
        elif ext == "kmp":
            print(json.dumps(read_kmp(path), indent=1))
        elif ext == "ksc":
            print("\n".join(read_ksc(path)))
        else:
            sys.exit("unknown extension")
    elif cmd == "wav":
        ksf_to_wav(read_ksf(path), argv[3])
    elif cmd == "keymap":
        kmp = read_kmp(path); base = os.path.dirname(os.path.abspath(path))
        folder = os.path.join(base, os.path.splitext(os.path.basename(path))[0])
        for r in kmp["regions"]:
            for cand in (os.path.join(base, r["sample"]), os.path.join(folder, r["sample"])):
                if os.path.exists(cand):
                    k = read_ksf(cand)
                    if k["empty"]:
                        break
                    r["rate"] = k["rate"]; r["loop_start"] = k["loop_start"]; r["loop_end"] = k["loop_end"]
                    ksf_to_wav(k, os.path.join(base, os.path.splitext(r["sample"])[0] + ".wav"))
                    r["wav"] = os.path.splitext(r["sample"])[0] + ".wav"; break
        json.dump(kmp, open(argv[3], "w"), indent=1)
    elif cmd == "export":
        mp, skipped = export_kmp(path, argv[3])
        print("wrote", mp)
        for sname, why in skipped:
            print("  skipped", sname, "-", why)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)

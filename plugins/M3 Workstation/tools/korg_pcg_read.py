#!/usr/bin/env python3
"""Read Korg PCG files: walk the chunk tree, list banks and the program / combination names.

Layout (all big-endian), verified on two real layouts:
  file:  'KORG' + 12 more header bytes, then chunks: id(4) + size(4) + data
  PCG1 contains: DIV1, INI1, PRG1, CMB1, DKT1, WSQ1, ARP1, GLB1, CSM1 ... (only some are containers)
  PRG1 / CMB1 / DKT1 contain bank chunks:
      PBK1 or MBK1 = program bank, CBK1 = combination bank, DBK1 = drum kit bank
  bank chunk data: count(u32) recordSize(u32) flags(u32) then count*recordSize bytes
  every program / combination record starts with a 16-byte name.
Seen: 540-byte programs in PBK1 and 448-byte combinations in CBK1 (exbpcm5 files);
      604-byte programs in MBK1 (TimoMoss.PCG).
      Triton LE / Triton Studio preload / 808 kick PCGs match the 540/448 layout; Triton LE also has
      DKT1/DBK1 (16 x 4112-byte drum kits). Parameter bytes are not decoded here.
Usage: python3 -I korg_pcg_read.py FILE.PCG [--csv out.csv] [--json out.json]
"""
import argparse, csv, json, struct, sys

# Product-id byte at header offset 4. Only values seen in real files or in the Korg forum
# (Oasys 0x70) are listed; everything else is reported as unknown, never guessed.
# 0x50 does NOT identify the model: it appears on 540-byte PBK1 files (Triton Studio/LE) AND on the
# 604-byte MBK1 file. Use the bank chunk id + record size to tell layouts apart.
PRODUCT_IDS = {0x50: "seen on Triton-family (540-byte PBK1) and 604-byte MBK1 files; not model-specific",
               0x70: "Oasys (per Korg forum thread; no local file)"}
FILE_TYPES = {0x00: "PCG"}

CONTAINERS = {b"PCG1", b"PRG1", b"CMB1", b"DKT1", b"WSQ1"}
BANKS = {b"PBK1": "program", b"MBK1": "program", b"CBK1": "combination", b"DBK1": "drumkit",
         b"WBK1": "waveseq"}  # WBK1 listed by the Oasys forum thread; untested locally


def header_info(d):
    pid, ftype = d[4], d[5]
    return {"product_id": pid, "product": PRODUCT_IDS.get(pid, "unknown"),
            "file_type": FILE_TYPES.get(ftype, f"unknown (0x{ftype:02x})"), "header_hex": d[:16].hex(" ")}


def name_len(d, base, rec, count):
    """16 on every verified file. Report 24 only if bytes 16..23 look like text in most records
    (a heuristic for longer-name models; unverified)."""
    n = min(count, 64); text = 0
    for i in range(n):
        seg = d[base + i * rec + 16:base + i * rec + 24]
        if all(32 <= c < 127 for c in seg) and seg.strip():
            text += 1
    return 24 if n and text * 10 >= n * 9 else 16


def walk(d, pos, end, depth, tree):
    while pos + 8 <= end:
        cid = d[pos:pos + 4]
        size = struct.unpack(">I", d[pos + 4:pos + 8])[0]
        if not all(32 <= c < 127 for c in cid) or pos + 8 + size > end:
            break
        tree.append((depth, pos, cid, size))
        if cid in CONTAINERS:
            walk(d, pos + 8, pos + 8 + size, depth + 1, tree)
        pos += 8 + size


def read_pcg(path):
    d = open(path, "rb").read()
    if d[:4] != b"KORG":
        raise ValueError("not a Korg file (no KORG header)")
    tree = []
    walk(d, 16, len(d), 0, tree)
    banks = []
    for depth, pos, cid, size in tree:
        if cid not in BANKS:
            continue
        count, rec, flags = struct.unpack(">III", d[pos + 8:pos + 20])
        base = pos + 20
        if count * rec + 12 > size:
            continue
        entry = {"chunk": cid.decode(), "kind": BANKS[cid], "count": count, "record_size": rec, "flags": flags, "offset": base, "names": []}
        entry["name_len"] = name_len(d, base, rec, count)
        if BANKS[cid] in ("program", "combination"):
            for i in range(count):
                entry["names"].append(d[base + i * rec:base + i * rec + entry["name_len"]].decode("latin-1").rstrip(" \0"))
        banks.append(entry)
    return d, tree, banks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path"); ap.add_argument("--csv"); ap.add_argument("--json")
    a = ap.parse_args()
    d, tree, banks = read_pcg(a.path)
    print(f"{a.path}: {len(d)} bytes")
    h = header_info(d)
    print(f"header: {h['header_hex']}  product 0x{h['product_id']:02x} = {h['product']}; type {h['file_type']}")
    print("chunks:")
    for depth, pos, cid, size in tree:
        print(f"  {'  ' * depth}{cid.decode()} @ {pos} size {size}")
    print("banks:")
    counters = {}
    for b in banks:
        n = counters.get(b["kind"], 0); counters[b["kind"]] = n + 1; b["number"] = n
        print(f"  {b['kind']} bank {n}: {b['count']} x {b['record_size']} bytes" + (f", first: {b['names'][:3]}" if b["names"] else ""))
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f); w.writerow(["kind", "bank", "index", "name"])
            for b in banks:
                for i, nm in enumerate(b["names"]):
                    w.writerow([b["kind"], b["number"], i, nm])
        print("wrote", a.csv)
    if a.json:
        json.dump(banks, open(a.json, "w"), indent=1); print("wrote", a.json)


if __name__ == "__main__":
    main()

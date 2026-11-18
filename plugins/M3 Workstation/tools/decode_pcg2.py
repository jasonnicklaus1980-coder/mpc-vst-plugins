#!/usr/bin/env python3
"""Decode a Korg PCG program bank using pcg_maps.json (derived from TimoMoss.PCG + its PDF).
Fields: category, osc1, osc2, insert1, insert2, master1, master2. Unknown byte values print as 0xNN.
Maps only contain values seen in the training bank; other banks may hit unknown values.
Usage: python3 -I decode_pcg2.py file.PCG [--json out.json]"""
import json,sys,os
here=os.path.dirname(os.path.abspath(__file__))
M=json.load(open(os.path.join(here,"pcg_maps.json")))
def main():
    data=open(sys.argv[1],"rb").read(); out=[]
    for i in range(128):
        r=data[172+i*604:172+(i+1)*604]
        d={"index":i,"name":r[:16].decode("latin-1").rstrip(" \x00")}
        for f,o in M["offsets"].items():
            d[f]=M["maps"][f].get(str(r[o]),"0x%02x"%r[o])
        out.append(d)
    if "--json" in sys.argv: json.dump(out,open(sys.argv[sys.argv.index("--json")+1],"w"),indent=1)
    else:
        for d in out[:6]: print(d)
main()

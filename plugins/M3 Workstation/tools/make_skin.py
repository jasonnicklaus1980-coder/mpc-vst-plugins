#!/usr/bin/env python3
"""Original 'virtual hardware workstation' skin for M3 Workstation (silver chassis, dark LCD, 61-key
keyboard picture). No third-party artwork or logos. Controls are the SDK's own widgets, translated into the LCD.
usage: make_skin.py PLUGIN_DIR SDK_DIR OUT_SKINS_DIR"""
import copy, json, math, os, random, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

plug, sdk, out = sys.argv[1:4]
W, H = 1280, 628
BLUE = (79, 156, 240); LCD = (38, 41, 46); LCD2 = (28, 30, 34); LINE = (78, 84, 94); TXT = (214, 220, 228); DIM = (128, 136, 148)
FB = "/usr/share/fonts/truetype/liberation/LiberationSansNarrow-Bold.ttf"
FR = "/usr/share/fonts/truetype/liberation/LiberationSansNarrow-Regular.ttf"
F = lambda p, s: ImageFont.truetype(p, s)
random.seed(7)

# ---- realtime-knob film strip: silver cap, dark skirt, indicator line -------------------------------
def make_knob_strip(path, size=64, frames=127, ss=4):
    S = size * ss; c = S / 2
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(base)
    d.ellipse([c - S*.49, c - S*.45, c + S*.49, c + S*.53], fill=(0, 0, 0, 90))            # drop shadow
    d.ellipse([c - S*.46, c - S*.46, c + S*.46, c + S*.46], fill=(34, 36, 40, 255))         # dark skirt
    for i in range(40):                                                                      # radial silver gradient
        k = i / 39; r = S * .40 * (1 - k * .9); g = int(150 + 85 * (1 - abs(k - .3)))
        d.ellipse([c - r, c - r - S*.015, c + r, c + r - S*.015], fill=(g, g + 2, g + 6, 255))
    d.ellipse([c - S*.40, c - S*.40, c + S*.40, c + S*.40], outline=(70, 74, 80, 255), width=ss)
    strip = Image.new("RGBA", (size, size * frames), (0, 0, 0, 0))
    for f in range(frames):
        a = math.radians(-135 + 270 * f / (frames - 1)); im = base.copy(); dd = ImageDraw.Draw(im)
        x0, y0 = c + math.sin(a) * S * .16, c - math.cos(a) * S * .16
        x1, y1 = c + math.sin(a) * S * .36, c - math.cos(a) * S * .36
        dd.line([x0, y0, x1, y1], fill=(25, 27, 31, 255), width=int(S * .045))
        strip.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    strip.save(path)

# ---- brushed-metal chassis --------------------------------------------------------------------------
def chassis():
    im = Image.new("RGB", (W, H)); px = im.load()
    for y in range(H):
        base = 188 - int(22 * abs(y - 120) / H) + random.randint(-3, 3)
        for x in range(W): px[x, y] = (base + random.randint(-2, 2),) * 3
    im = im.filter(ImageFilter.GaussianBlur(0.4))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W - 1, H - 1], outline=(40, 42, 46), width=6)                       # dark bevel
    d.rectangle([6, 6, W - 7, H - 7], outline=(215, 217, 220), width=1)
    return im

def recess(d, box, fill, r=8):
    x0, y0, x1, y1 = box
    d.rounded_rectangle([x0 - 5, y0 - 5, x1 + 5, y1 + 5], r + 4, fill=(30, 32, 36))          # dark bezel
    d.rounded_rectangle([x0 - 5, y0 - 5, x1 + 5, y1 + 5], r + 4, outline=(235, 236, 238), width=1)
    d.rounded_rectangle(box, r, fill=fill)

# ---- layout (canvas coordinates) --------------------------------------------------------------------
RT = [(40, 26), (140, 26), (240, 26), (340, 26)]      # realtime knob tiles 90 x 96
RT_PARAMS = ["Cutoff", "Resonance", "Attack", "Release"]
GRID = [(246 + c * 300, 176 + r * 128) for r in range(2) for c in range(3)]   # 278 x 107 tiles, 3 x 2

def background(tab, names, nslots=6):
    im = chassis(); d = ImageDraw.Draw(im)
    for i, (x, y) in enumerate(RT):                                                       # realtime control wells
        d.ellipse([x + 9, y - 6, x + 81, y + 66], fill=(60, 63, 68)); d.ellipse([x + 12, y - 3, x + 78, y + 63], fill=(24, 25, 28))
        t = "REALTIME CONTROL %d" % (i + 1); f = F(FB, 10)
        d.text((x + 45 - d.textlength(t, font=f) / 2, y - 20), t, font=f, fill=(50, 52, 58))
    recess(d, (440, 20, 1000, 118), LCD2)                                                  # header LCD
    d.text((456, 28), "SHANNON", font=F(FB, 13), fill=DIM)
    d.text((456, 46), "M3 WORKSTATION", font=F(FB, 30), fill=TXT)
    d.text((458, 88), "PROGRAM  ·  %s PAGE" % tab["tabName"], font=F(FR, 15), fill=BLUE)
    recess(d, (28, 138, 1252, 496), LCD, r=10)                                             # main LCD
    d.rectangle([28, 138, 1252, 166], fill=LCD2); d.line([28, 166, 1252, 166], fill=LINE)
    x = 40
    for n in names:                                                                        # screen tabs
        w = int(d.textlength(n, font=F(FB, 15))) + 28; on = n == tab["tabName"]
        d.rounded_rectangle([x, 142, x + w, 166], 4, fill=(58, 96, 150) if on else (52, 56, 63), outline=LINE)
        d.text((x + 14, 146), n, font=F(FB, 15), fill=(255, 255, 255) if on else TXT); x += w + 5
    d.rectangle([38, 176, 232, 488], fill=LCD2, outline=LINE)                             # program list (picture)
    d.rectangle([38, 176, 232, 198], fill=(46, 50, 58)); d.text((48, 180), "PRESETS", font=F(FB, 13), fill=DIM)
    for i, (txt, col) in enumerate((("Choose presets with", TXT), ("the MPC preset browser.", TXT), ("", TXT),
                                    ("116 factory presets", BLUE), ("19 categories", DIM), ("+ your user presets", BLUE),
                                    ("", TXT), ("Realtime knobs work", DIM), ("on every page.", DIM))):
        if txt: d.text((48, 208 + i * 24), txt, font=F(FR, 14), fill=col)
    for (x, y) in GRID[:nslots]:                                                          # a panel only where a control sits
        d.rounded_rectangle([x - 6, y - 6, x + 284, y + 113], 6, fill=(31, 34, 39), outline=LINE)
    gx, gy, gw, gh = 246, 424, 884, 64                                                     # graph strip
    d.rectangle([gx, gy, gx + gw, gy + gh], fill=(22, 24, 27), outline=LINE)
    for i in range(1, 8): d.line([gx + i * gw // 8, gy, gx + i * gw // 8, gy + gh], fill=(40, 44, 50))
    nm = tab["tabName"]; pts = {"ENV": [(0, 1), (.1, 0), (.35, .5), (.7, .5), (1, 1)], "FILTER": [(0, .3), (.55, .3), (.65, .08), (.78, .55), (1, .95)]}.get(nm)
    if nm in ("OSC", "SOURCE", "LFO"): pts = [(i / 120, .5 - .4 * math.sin(i / 120 * 6.2832 * (3 if nm == "OSC" else 1.5))) for i in range(121)]
    if nm.startswith("ARP"): pts = [(i / 16, 1 - (.25 + .15 * ((i * 5) % 4))) if False else (i / 16, 0.8 - 0.18 * (i % 4)) for i in range(17)]   # stepped arp pattern
    if nm == "COMBI": pts = [(0, .75), (.5, .75), (.5, .25), (1, .25)]   # a keyboard split: two zones
    if nm == "DRUM": pts = [(i / 100, 1 - 0.9 * math.exp(-i / 14) * (0.6 + 0.4 * math.cos(i / 4))) for i in range(101)]   # decaying hit
    if nm == "FX": pts = [(0, 1), (.02, .05)] + [(.02 + i / 100 * .98, 1 - .9 * math.exp(-i / 18) * (1 if i % 25 < 3 else .45)) for i in range(1, 101)]   # impulse + decaying tail
    if not pts: pts = [(0, .5), (1, .5)]   # pages without a specific picture get a flat line, never a crash
    d.line([(gx + 10 + p[0] * (gw - 20), gy + 6 + p[1] * (gh - 12)) for p in pts], fill=(150, 200, 255), width=2)
    d.text((gx + 8, gy + 3), nm, font=F(FB, 11), fill=DIM)
    # keyboard: 61 keys C2..C6 (36 white keys)
    ky0, ky1, kx0, kx1 = 512, 618, 16, 1264; ww = (kx1 - kx0) / 36
    d.rectangle([kx0 - 2, ky0 - 3, kx1 + 2, ky1 + 2], fill=(20, 21, 24))
    for i in range(36):
        x = kx0 + i * ww
        for k in range(ky1 - ky0): g = 246 - int(28 * k / (ky1 - ky0)); d.line([x + 1, ky0 + k, x + ww - 1, ky0 + k], fill=(g, g, g - 3))
        d.line([x, ky0, x, ky1], fill=(60, 60, 64))
    for i in range(36):                                                                    # black keys after C D F G A
        if i % 7 in (0, 1, 3, 4, 5) and i < 35:
            x = kx0 + (i + 1) * ww - ww * .3
            d.rectangle([x, ky0, x + ww * .6, ky0 + 66], fill=(18, 18, 20)); d.rectangle([x + 2, ky0, x + ww * .6 - 2, ky0 + 58], fill=(46, 47, 52))
    return im

# ---- header buttons: images carry their own label; each is bound to a real parameter ----------------
def make_button(path, label, on):
    im = Image.new("RGBA", (46, 38), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, 45, 37], 6, fill=(58, 96, 150) if on else (70, 74, 82), outline=(30, 32, 36), width=1)
    d.line([4, 2, 41, 2], fill=(130, 175, 235) if on else (110, 115, 124))
    f = F(FB, 11); d.text((23 - d.textlength(label, font=f) / 2, 11), label, font=f, fill=(255, 255, 255) if on else TXT)
    im.save(path)
HEADER = [("SYNTH", "Source", 0, 3), ("SAMPLE", "Source", 1, 3), ("DRUM", "Source", 2, 3), ("LOOP", "Loop", 1, 1), ("LFO ON", "LFO On", 1, 1)]

# ---- build ---------------------------------------------------------------------------------------------
tmp = out + ".tmp"; shutil.rmtree(tmp, ignore_errors=True)
subprocess.check_call([sys.executable, os.path.join(sdk, "tools/gen_ui.py"), "--params", os.path.join(plug, "build/params.json"),
                       "--ui", os.path.join(plug, "ui.json"), "--out", tmp, "--quiet"])
tui = json.load(open(os.path.join(tmp, "TUI.json"))); pd = tui["pageData"]; tabs = pd["tabs"]
names = [t["tabName"] for t in tabs]
params = json.load(open(os.path.join(plug, "build/params.json")))
plist = params["params"]
pidx = {p["name"]: i for i, p in enumerate(plist)}

tmpl = next(e for e in pd["componentDefinitions"]["localComponentDefinitions"] if e["key"] == "knobUni")["value"]
rt = copy.deepcopy(tmpl)
for c in rt["componentsData"]:
    n = c["componentData"]["name"]
    if n == "focus": c["bounds"]["bounds"] = "0 0 90 96"
    elif n == "knob": c["bounds"]["bounds"] = "13 0 64 64"; c["componentData"]["data"]["filmStrip"] = "knob_rt.png"
    elif n == "value": c["bounds"]["bounds"] = "0 66 90 26"; c["componentData"]["data"]["textStyle"]["justification"] = "horizontallyCentred verticallyCentred"; c["componentData"]["data"]["textStyle"]["font"]["height"] = 18.0
rt["componentsData"] = [c for c in rt["componentsData"] if c["componentData"]["name"] != "name"]
pd["componentDefinitions"]["localComponentDefinitions"].append({"key": "rtKnob", "value": rt})

os.makedirs(out, exist_ok=True)
for f in os.listdir(tmp): shutil.copy(os.path.join(tmp, f), out)
make_knob_strip(os.path.join(out, "knob_rt.png"))
for i, (lab, pn, bid, n) in enumerate(HEADER):
    make_button(os.path.join(out, "hdr%d_on.png" % i), lab, True); make_button(os.path.join(out, "hdr%d_off.png" % i), lab, False)

for t in tabs:
    comps = t["componentDefinition"]["componentsData"]
    slot, delta = 0, {}
    sample = next(c for c in comps if c["componentData"]["type"] != "Label")
    for c in comps:                                           # move tiles into the LCD grid
        if c["componentData"]["type"] == "Label": continue
        x, y, w, h = map(int, c["bounds"]["bounds"].split()); nx, ny = GRID[slot]; slot += 1
        delta[c["componentData"]["name"]] = (nx - x, ny - y); c["bounds"]["bounds"] = "%d %d %d %d" % (nx, ny, w, h)
    for c in comps:
        if c["componentData"]["type"] != "Label": continue
        parent = c["componentData"]["name"].rsplit(" seg ", 1)[0]
        if parent in delta:
            x, y, w, h = map(int, c["bounds"]["bounds"].split()); dx, dy = delta[parent]; c["bounds"]["bounds"] = "%d %d %d %d" % (x + dx, y + dy, w, h)
    for (x, y), pn in zip(RT, RT_PARAMS):                     # four realtime knobs, same on every page
        e = copy.deepcopy(sample)
        e["componentData"]["name"] = "Realtime " + pn; e["componentData"]["type"] = "rtKnob"
        e["handle remapping"]["map"] = [{"key": "Data", "value": "Parameter %d" % pidx[pn]}]
        e["bounds"]["bounds"] = "%d %d 90 96" % (x, y); e["bounds"]["acceptsHWFocus"] = "No"
        comps.append(e)
    for i, (lab, pn, bid, n) in enumerate(HEADER):            # real header buttons
        e = copy.deepcopy(sample)
        e["componentData"] = {"version": 1, "name": "Header " + lab, "type": "Button", "data": {"version": 2,
            "onImage": "hdr%d_on.png" % i, "offImage": "hdr%d_off.png" % i, "buttonId": bid, "numButtonsInGroup": n,
            "handleName": "Data", "gestureBehaviour": "Instant"}}
        e["handle remapping"]["map"] = [{"key": "Data", "value": "Parameter %d" % pidx[pn]}]
        e["bounds"]["bounds"] = "%d 40 46 38" % (1012 + i * 49); e["bounds"]["acceptsHWFocus"] = "No"
        comps.append(e)
    fn = "bg_%s.png" % t["tabName"].lower(); background(t, names, slot).save(os.path.join(out, fn))
    for k in ("focussed", "unfocussed"): t["componentDefinition"]["backgroundData"][k]["image"] = fn
json.dump(tui, open(os.path.join(out, "TUI.json"), "w"), indent=1)
shutil.rmtree(tmp); print("wrote", out)

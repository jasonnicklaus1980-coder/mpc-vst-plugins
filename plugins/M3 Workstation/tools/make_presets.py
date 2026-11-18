#!/usr/bin/env python3
"""Generates src/Presets.inc (C++ table) and presets.json (name, category, description, tags, values) for M3 Workstation.
Every preset is a hand-written parameter set (no random variation). Volume is level-matched by calibrate step (preset_gain.json).
usage: make_presets.py PLUGIN_DIR"""
import json, os, sys
plug = sys.argv[1]
ORDER = ["vol","osc1","osc2","semi","detune","mix","cutoff","reso","fenv","atk","dec","sus","rel","source","inst","loop","ftype","ktrack",
         "lfoshape","lforate","lfodepth","lfodest","lfoon","chorus","dlymix","dlytime","dlyfb","revmix","revsize","arpon","arpmode","arprate",
         "arpgate","arpoct","arpswing","arplatch","drumtune","drumdecay","drumkit","mode","combi"]
D = dict(vol=-6, osc1=2, osc2=2, semi=0, detune=7, mix=50, cutoff=3000, reso=20, fenv=40, atk=5, dec=250, sus=70, rel=300, source=0, inst=1, loop=1,
         ftype=0, ktrack=0, lfoshape=0, lforate=5, lfodepth=0, lfodest=0, lfoon=1, chorus=0, dlymix=0, dlytime=350, dlyfb=35, revmix=0, revsize=50,
         arpon=0, arpmode=0, arprate=3, arpgate=80, arpoct=1, arpswing=0, arplatch=0, drumtune=0, drumdecay=100, drumkit=0, mode=0, combi=0)
SIN, TRI, SAW, SQ = 0, 1, 2, 3
PITCH, CUT, AMP = 0, 1, 2
SH = 3
LP, HP, BP, NO = 0, 1, 2, 3
UP, DOWN, UPDN, RND, ORD, CHORD = 0, 1, 2, 3, 4, 5
R14, R18, R18T, R116, R116T, R132 = 0, 1, 2, 3, 4, 5
P = []
def add(cat, tag, name, desc, tags, **kw):
    assert all(k in D for k in kw), kw
    n = "%s %s" % (tag, name); assert len(n) <= 23, n
    P.append(dict(category=cat, name=n, description=desc, tags=tags.split(), v={**D, **kw}))
def drum(cat, tag, name, desc, tags, **kw):
    add(cat, tag, name, desc, tags, source=2, atk=1, dec=250, sus=100, rel=300, vol=-4, **kw)

# ---- Piano (synth approximations; no sampled pianos) ----
C="Piano"; T="PNO"
add(C,T,"Felt Tine","Soft felt-hammer tine tone, mellow and close.","soft felt intimate",osc1=TRI,osc2=SIN,semi=12,mix=30,cutoff=2500,reso=5,fenv=30,atk=1,dec=900,sus=25,rel=350,ktrack=60,revmix=15)
add(C,T,"Bright Hammer","Brighter hammer attack with fast decay for pop playing.","bright pop attack",osc1=SAW,osc2=TRI,semi=12,mix=25,cutoff=5000,reso=5,fenv=50,atk=1,dec=700,sus=15,rel=300,ktrack=70,chorus=10,revmix=12)
add(C,T,"Dark Grand","Round, dark body with a long sustain and a bit of room.","dark warm ballad",osc1=TRI,osc2=SIN,semi=-12,mix=40,cutoff=1800,reso=5,fenv=25,atk=1,dec=1200,sus=30,rel=500,ktrack=50,revmix=25)
add(C,T,"Honky Tonk","Two slightly out-of-tune strings for a saloon feel.","detuned saloon vintage",osc1=SAW,osc2=SAW,detune=22,cutoff=3500,reso=5,fenv=40,atk=1,dec=600,sus=10,rel=250,chorus=40,revmix=10)
add(C,T,"Layered Keys","Combination: a pad under a plucked attack. Plays in Combi mode.","layer combi pad",mode=1,combi=0,revmix=20)
add(C,T,"Toy Piano","Small bright metal-bar toy piano.","toy bright cute",osc1=SQ,osc2=SIN,semi=12,mix=40,cutoff=6000,reso=10,fenv=40,atk=1,dec=350,sus=0,rel=200,dlymix=10)
# ---- Electric Piano ----
C="Electric Piano"; T="EP"
add(C,T,"Tine EP","Classic tine electric piano with slow tremolo.","tine tremolo classic",osc1=SIN,osc2=SIN,semi=12,mix=20,cutoff=3500,reso=5,fenv=35,atk=1,dec=800,sus=30,rel=400,lfoshape=0,lforate=5,lfodepth=25,lfodest=AMP,chorus=30,revmix=10)
add(C,T,"Suitcase","Warm suitcase-style EP with stereo tremolo feel.","warm tremolo suitcase",osc1=TRI,osc2=TRI,detune=5,cutoff=2200,reso=15,fenv=20,atk=1,dec=700,sus=40,rel=300,lfoshape=1,lforate=4.5,lfodepth=35,lfodest=AMP,chorus=20)
add(C,T,"Wurly Bark","Reedy EP that barks when you play hard.","reed bark gritty",osc1=SQ,osc2=TRI,semi=12,mix=35,cutoff=2000,reso=30,fenv=45,atk=1,dec=500,sus=30,rel=250,chorus=25)
add(C,T,"Glass EP","Glassy, shimmering EP with a long tail.","glass shimmer",osc1=SIN,osc2=SAW,semi=12,mix=25,cutoff=6000,reso=5,fenv=35,atk=1,dec=1000,sus=20,rel=500,revmix=25,dlymix=15)
add(C,T,"Phase EP","Detuned EP with a slowly sweeping filter.","phase sweep chorus",osc1=TRI,osc2=TRI,detune=20,cutoff=3000,reso=15,fenv=25,atk=2,dec=700,sus=40,rel=350,lfoshape=0,lforate=0.8,lfodepth=20,lfodest=CUT,chorus=60)
add(C,T,"Dream EP","Slow-attack dreamy EP wrapped in chorus and reverb.","dreamy ambient soft",osc1=SIN,osc2=TRI,semi=12,detune=10,mix=40,cutoff=2800,reso=5,fenv=20,atk=8,dec=1200,sus=45,rel=800,chorus=45,revmix=40,dlymix=20)
# ---- Organ ----
C="Organ"; T="ORG"
add(C,T,"Drawbar Full","Full drawbar organ with a rotary-speaker-style wobble.","drawbar rotary full",osc1=SQ,osc2=SQ,semi=12,detune=3,cutoff=9000,reso=0,fenv=0,atk=3,dec=10,sus=100,rel=40,lfoshape=0,lforate=6.5,lfodepth=6,lfodest=PITCH,chorus=50)
add(C,T,"Perc Organ","Percussive jazz organ with a bright upper partial.","percussive jazz bright",osc1=SIN,osc2=SIN,semi=12,detune=2,mix=45,cutoff=12000,reso=0,fenv=0,atk=2,dec=150,sus=100,rel=50,lfodepth=5,lfodest=PITCH,lforate=6,revmix=15)
add(C,T,"Rock Grind","Overdriven rock organ with resonant bite.","rock grit overdrive",osc1=SAW,osc2=SQ,detune=4,cutoff=4500,reso=25,fenv=0,atk=2,dec=10,sus=100,rel=60,lfodepth=12,lfodest=AMP,lforate=6,chorus=30)
add(C,T,"Church","Soft pipe-organ-like tone in a big room.","pipe church reverb",osc1=SIN,osc2=SIN,semi=12,mix=50,cutoff=5000,reso=0,fenv=0,atk=15,dec=10,sus=100,rel=600,revmix=70,revsize=85)
add(C,T,"Jazz Combo","Thin, smooth combo organ that tracks the keyboard.","jazz smooth thin",osc1=TRI,osc2=SIN,semi=12,mix=40,cutoff=3000,reso=5,fenv=0,atk=3,dec=10,sus=100,rel=80,ktrack=40,lfodepth=10,lfodest=AMP,chorus=35)
add(C,T,"Slow Whirl","Slow-rotor organ with heavy chorus.","slow rotor wash",osc1=SAW,osc2=SAW,semi=12,detune=9,cutoff=3500,reso=5,fenv=0,atk=4,dec=10,sus=100,rel=100,lfoshape=0,lforate=0.9,lfodepth=15,lfodest=PITCH,chorus=70,revmix=30)
# ---- Bass ----
C="Bass"; T="BAS"
add(C,T,"Sub Pillow","Pure rounded sub bass for low support.","sub round clean",osc1=SIN,osc2=SIN,semi=-12,mix=35,cutoff=600,reso=0,fenv=0,atk=3,dec=200,sus=100,rel=120)
add(C,T,"Moog Round","Fat analog-style bass with a punchy filter sweep.","analog fat punch",osc1=SAW,osc2=SQ,semi=-12,detune=4,cutoff=700,reso=35,fenv=60,atk=1,dec=250,sus=40,rel=100,ktrack=20)
add(C,T,"Acid Line","Squelchy resonant line with a fast filter snap.","acid squelch resonant",osc1=SAW,mix=0,cutoff=500,reso=75,fenv=85,atk=1,dec=180,sus=0,rel=80,dlymix=20)
add(C,T,"Pluck Bass","Short plucked bass, tight and percussive.","pluck tight short",osc1=SQ,osc2=TRI,semi=12,mix=30,cutoff=1500,reso=15,fenv=55,atk=1,dec=200,sus=0,rel=120)
add(C,T,"Fat Unison","Two detuned saws an octave down for a thick low end.","thick detuned unison",osc1=SAW,osc2=SAW,semi=-12,detune=18,cutoff=900,reso=20,fenv=45,atk=2,dec=400,sus=70,rel=150,chorus=15)
add(C,T,"Rubber Bass","Bouncy funk bass with a strong resonant pop.","funk bouncy resonant",osc1=TRI,osc2=SQ,semi=-12,mix=40,cutoff=500,reso=55,fenv=80,atk=1,dec=160,sus=30,rel=90,ktrack=30)
# ---- Lead ----
C="Lead"; T="LD"
add(C,T,"Sync Scream","Cutting lead with vibrato and a little echo.","cutting vibrato solo",osc1=SAW,osc2=SAW,semi=7,detune=15,cutoff=6000,reso=30,fenv=30,atk=3,dec=200,sus=85,rel=200,lfodepth=20,lforate=5.5,lfodest=PITCH,dlymix=20,revmix=15)
add(C,T,"Soft Whistle","Breathy sine whistle with slow vibrato.","breathy soft whistle",osc1=SIN,osc2=TRI,semi=12,mix=25,cutoff=9000,reso=0,fenv=10,atk=40,dec=300,sus=90,rel=250,lfodepth=15,lforate=5,lfodest=PITCH,revmix=25)
add(C,T,"Square Mono","Chunky mono square lead with a rhythmic echo.","square mono chunky",osc1=SQ,osc2=SQ,semi=-12,detune=6,cutoff=3500,reso=25,fenv=40,atk=2,dec=250,sus=80,rel=150,dlymix=25,dlytime=400,dlyfb=40)
add(C,T,"Duo Saw","Wide detuned saw lead with chorus.","wide detuned saw",osc1=SAW,osc2=SAW,detune=25,cutoff=7000,reso=15,fenv=20,atk=3,dec=200,sus=90,rel=300,chorus=55,revmix=20)
add(C,T,"Hollow Lead","Band-passed, hollow reedy lead.","hollow reedy bandpass",osc1=TRI,osc2=SQ,semi=12,ftype=BP,cutoff=1800,reso=50,fenv=25,atk=5,dec=250,sus=85,rel=200,revmix=15)
add(C,T,"Rez Sweep","Resonant lead with a slow filter wobble.","resonant sweep slow",osc1=SAW,osc2=SAW,detune=10,cutoff=1500,reso=55,fenv=20,atk=4,dec=300,sus=90,rel=250,lfoshape=1,lforate=0.5,lfodepth=60,lfodest=CUT,dlymix=15)
# ---- Pad ----
C="Pad"; T="PAD"
add(C,T,"Warm Pad","Warm, slow, wide pad for chords.","warm slow wide",osc1=SAW,osc2=TRI,detune=12,cutoff=1600,reso=10,fenv=25,atk=500,dec=1000,sus=85,rel=1200,chorus=50,revmix=40)
add(C,T,"Glass Pad","Airy glassy pad with a long reverb tail.","glass airy bright",osc1=SIN,osc2=TRI,semi=12,mix=40,cutoff=5000,reso=5,fenv=10,atk=700,dec=1000,sus=90,rel=1500,chorus=35,revmix=60,revsize=80)
add(C,T,"Evolving","Slowly moving filter pad that never sits still.","evolving slow motion",osc1=SAW,osc2=SAW,detune=15,cutoff=1200,reso=25,fenv=10,atk=1200,dec=1000,sus=100,rel=2000,lfodepth=50,lforate=0.2,lfodest=CUT,revmix=60)
add(C,T,"Choir Pad","Vocal-ish band-passed pad.","choir vocal bandpass",osc1=TRI,osc2=SIN,semi=12,ftype=BP,cutoff=1200,reso=35,fenv=15,atk=900,dec=1000,sus=100,rel=1500,chorus=40,revmix=55)
add(C,T,"Dark Drone","Low, dark pad that slowly breathes.","dark drone low",osc1=SAW,osc2=SQ,semi=-12,detune=8,cutoff=500,reso=30,fenv=10,atk=1500,dec=1000,sus=100,rel=2500,lfodepth=30,lforate=0.1,lfodest=CUT,revmix=70)
add(C,T,"Ice Pad","High-passed icy pad with long echoes.","ice airy cold",osc1=TRI,osc2=TRI,semi=12,detune=20,mix=45,ftype=HP,cutoff=400,reso=10,fenv=0,atk=800,dec=1000,sus=100,rel=2000,revmix=65,dlymix=25,dlytime=500,dlyfb=50)
# ---- Strings ----
C="Strings"; T="STR"
add(C,T,"Ensemble","Full string ensemble with chorus.","ensemble full chorus",osc1=SAW,osc2=SAW,detune=14,cutoff=3000,reso=5,fenv=15,atk=250,dec=300,sus=90,rel=500,chorus=70,revmix=25,ktrack=40)
add(C,T,"Slow Strings","Slow-bowing string section.","slow bowed legato",osc1=SAW,osc2=SAW,detune=10,cutoff=2200,reso=5,fenv=15,atk=700,dec=500,sus=100,rel=900,chorus=60,revmix=35)
add(C,T,"Pizzicato","Plucked string section, short and bouncy.","pizzicato short bouncy",osc1=SAW,osc2=TRI,cutoff=3500,reso=10,fenv=40,atk=1,dec=150,sus=0,rel=120,revmix=15,ktrack=50)
add(C,T,"Tremolo Str","Trembling strings for tension and drama.","tremolo tension",osc1=SAW,osc2=SAW,detune=12,cutoff=3000,reso=5,fenv=10,atk=200,dec=300,sus=100,rel=400,lfoshape=0,lforate=8,lfodepth=55,lfodest=AMP,chorus=40,revmix=25)
add(C,T,"Solo Cello","Solo cello-like voice with vibrato.","solo cello vibrato",osc1=SAW,osc2=SQ,semi=-12,detune=4,cutoff=1500,reso=10,fenv=30,atk=90,dec=300,sus=95,rel=300,lfodepth=12,lforate=5.5,lfodest=PITCH,revmix=25)
add(C,T,"Layer Stack","Combination: pad, pluck and an octave-up lead stacked.","layer stack combi",mode=1,combi=3,revmix=25)
# ---- Brass ----
C="Brass"; T="BRS"
add(C,T,"Brass Stab","Short punchy brass hit.","stab punchy short",osc1=SAW,osc2=SAW,detune=6,cutoff=1200,reso=15,fenv=60,atk=25,dec=250,sus=60,rel=150,chorus=15,revmix=15)
add(C,T,"Soft Horns","Gentle mellow horn section.","soft mellow horn",osc1=TRI,osc2=SAW,cutoff=1500,reso=5,fenv=45,atk=80,dec=300,sus=80,rel=200,revmix=20,chorus=20)
add(C,T,"Synth Brass","Big 80s-style synth brass.","synth big 80s",osc1=SAW,osc2=SAW,detune=12,cutoff=2000,reso=20,fenv=70,atk=40,dec=350,sus=70,rel=250,chorus=40,revmix=20)
add(C,T,"Trumpet Lead","Solo trumpet-like lead with vibrato.","trumpet solo vibrato",osc1=SAW,osc2=SQ,cutoff=2500,reso=25,fenv=55,atk=35,dec=250,sus=85,rel=150,lfodepth=8,lforate=5.5,lfodest=PITCH,revmix=15)
add(C,T,"Fanfare","Bright heroic brass with a wide hall.","fanfare heroic bright",osc1=SAW,osc2=SAW,semi=12,mix=40,cutoff=3000,reso=15,fenv=50,atk=20,dec=300,sus=80,rel=300,revmix=45,revsize=75)
add(C,T,"Mellow Bone","Low, round trombone-like tone.","trombone low round",osc1=TRI,osc2=SAW,semi=-12,cutoff=900,reso=10,fenv=50,atk=60,dec=300,sus=85,rel=200,revmix=25)
# ---- Synth ----
C="Synth"; T="SYN"
add(C,T,"Poly Pluck","Classic poly-synth pluck with a synced echo.","poly pluck echo",osc1=SAW,osc2=SQ,detune=10,cutoff=2000,reso=30,fenv=60,atk=1,dec=300,sus=25,rel=200,dlymix=25,dlytime=375,dlyfb=40,revmix=15)
add(C,T,"Resonant Keys","Squelchy resonant keys.","resonant keys chorus",osc1=SQ,osc2=SAW,semi=7,cutoff=1200,reso=60,fenv=70,atk=1,dec=250,sus=30,rel=200,chorus=25)
add(C,T,"Sweep Arp","Arpeggiated saw with a filter snap on every step.","arp sweep up",arpon=1,arpmode=UP,arprate=R116,arpgate=60,arpoct=2,osc1=SAW,osc2=SAW,cutoff=1800,reso=40,fenv=50,atk=1,dec=150,sus=0,rel=100,dlymix=30,revmix=20)
add(C,T,"Notch Texture","Moving notch-filter texture.","notch texture moving",osc1=SAW,osc2=SAW,detune=20,ftype=NO,cutoff=1500,reso=55,fenv=0,atk=200,dec=500,sus=90,rel=500,lfoshape=0,lforate=0.4,lfodepth=50,lfodest=CUT,chorus=40,revmix=30)
add(C,T,"Wobble Keys","Keys with a fast tri-wave filter wobble.","wobble rhythmic",osc1=SAW,osc2=SQ,semi=-12,cutoff=900,reso=45,fenv=0,atk=2,dec=200,sus=90,rel=150,lfoshape=1,lforate=3,lfodepth=70,lfodest=CUT)
add(C,T,"Hollow Arp","Up-down arpeggio through a band-pass filter.","arp hollow bandpass",arpon=1,arpmode=UPDN,arprate=R18,arpgate=70,arpoct=2,osc1=TRI,osc2=SQ,semi=12,ftype=BP,cutoff=2200,reso=40,fenv=30,atk=1,dec=200,sus=20,rel=150,dlymix=25,revmix=30)
add(C,T,"Split Bass+Lead","Combination: bass below middle C, lead above.","split bass lead combi",mode=1,combi=1,revmix=10)
add(C,T,"Velocity Switch","Combination: soft = pluck, hard = lead.","velocity switch combi",mode=1,combi=2,revmix=10)
# ---- Bells ----
C="Bells"; T="BEL"
add(C,T,"Glass Bell","Bright glass bell with a long ring.","glass bright ring",osc1=SIN,osc2=SIN,semi=12,detune=35,mix=30,cutoff=9000,reso=0,fenv=0,atk=1,dec=2500,sus=0,rel=1500,revmix=50,dlymix=25)
add(C,T,"Tubular","Deep tubular bell, slightly inharmonic.","tubular deep inharmonic",osc1=TRI,osc2=SIN,semi=12,detune=-35,mix=35,cutoff=5000,reso=10,fenv=0,atk=1,dec=3000,sus=0,rel=2000,revmix=45)
add(C,T,"Music Box","Tinkling music box with repeating echoes.","music box echo",osc1=SIN,osc2=TRI,semi=12,mix=40,cutoff=8000,reso=0,fenv=0,atk=1,dec=600,sus=0,rel=400,dlymix=35,dlytime=280,dlyfb=45,revmix=25)
add(C,T,"Celestial","Huge shimmering bell wash.","celestial shimmer huge",osc1=SIN,osc2=SIN,semi=12,detune=45,cutoff=7000,reso=0,fenv=0,atk=20,dec=2000,sus=20,rel=2500,chorus=50,revmix=75,revsize=90)
add(C,T,"Marimba Mallet","Woody mallet with a quick decay.","marimba mallet woody",osc1=SIN,osc2=SQ,semi=12,mix=20,cutoff=2500,reso=5,fenv=30,atk=1,dec=350,sus=0,rel=250,revmix=20)
add(C,T,"Vibes","Vibraphone-style bell with a slow motor tremolo.","vibes tremolo mellow",osc1=SIN,osc2=TRI,semi=12,mix=30,cutoff=4500,reso=0,fenv=0,atk=1,dec=1500,sus=0,rel=900,lfoshape=0,lforate=5.5,lfodepth=40,lfodest=AMP,revmix=30,chorus=20)
# ---- Plucks ----
C="Plucks"; T="PLK"
add(C,T,"Harp Pluck","Harp-like pluck with a long shimmering tail.","harp shimmer",osc1=TRI,osc2=SIN,semi=12,mix=30,cutoff=4000,reso=5,fenv=40,atk=1,dec=600,sus=0,rel=500,revmix=40,dlymix=20)
add(C,T,"Nylon","Nylon-string guitar-style pluck.","nylon guitar soft",osc1=TRI,osc2=SIN,mix=30,cutoff=2500,reso=5,fenv=35,atk=1,dec=450,sus=0,rel=300,revmix=20,ktrack=60)
add(C,T,"Kalimba","Thumb-piano pluck.","kalimba thumb piano",osc1=SIN,osc2=SIN,semi=12,mix=25,cutoff=6000,reso=0,fenv=0,atk=1,dec=500,sus=0,rel=400,revmix=25,dlymix=20)
add(C,T,"Zap Pluck","Sharp resonant zap with a fast sweep.","zap resonant sharp",osc1=SAW,osc2=SAW,detune=10,cutoff=800,reso=40,fenv=90,atk=1,dec=140,sus=0,rel=120,revmix=10)
add(C,T,"Koto","Bright plucked koto-like tone.","koto bright plucked",osc1=SQ,osc2=TRI,semi=7,mix=30,cutoff=3000,reso=10,fenv=50,atk=1,dec=350,sus=0,rel=250,revmix=25)
add(C,T,"Dub Pluck","Short pluck drowned in dub echoes.","dub echo pluck",osc1=SQ,osc2=SQ,semi=-12,cutoff=1500,reso=35,fenv=60,atk=1,dec=200,sus=0,rel=150,dlymix=50,dlytime=450,dlyfb=55,revmix=30)
# ---- Drums (built-in synthesized kits) ----
C="Drums"; T="DRM"
drum(C,T,"Tight Kit","Short, dry, punchy kit.","tight dry punchy",drumdecay=65,revmix=8)
drum(C,T,"808 Style Kit","Long sub kicks, low-tuned.","808 sub long",drumtune=-5,drumdecay=170,revmix=5)
drum(C,T,"Big Room Kit","Large, roomy kit with a big reverb.","big room reverb",drumdecay=120,revmix=35,revsize=70)
drum(C,T,"Dry Studio","Close-mic, completely dry kit.","dry studio close",drumdecay=80)
drum(C,T,"Pitched Up Kit","Everything tuned up for a lively, small kit.","bright pitched small",drumtune=4,drumdecay=90,revmix=15)
drum(C,T,"Roomy Kit","Natural room with a hint of echo.","room natural",drumdecay=110,revmix=22,revsize=45,dlymix=10)
# ---- Percussion ----
C="Percussion"; T="PRC"
drum(C,T,"Tom Lab","Low-tuned kit with long toms.","tom low long",drumtune=-3,drumdecay=150,revmix=20)
drum(C,T,"Cymbal Wash","Long cymbals soaking in reverb.","cymbal wash long",drumdecay=190,revmix=50,revsize=80)
drum(C,T,"Metal Kit","High-tuned, short, metallic kit with echo.","metal bright short",drumtune=7,drumdecay=60,dlymix=15)
add(C,T,"Wood Block","Short dry wood-block click.","wood block click",osc1=SQ,osc2=TRI,semi=12,mix=40,cutoff=3000,reso=10,fenv=20,atk=1,dec=70,sus=0,rel=60,revmix=10)
add(C,T,"Conga","Soft hand-drum thump.","conga hand drum",osc1=TRI,osc2=SIN,mix=20,cutoff=1500,reso=10,fenv=50,atk=1,dec=180,sus=0,rel=100,revmix=15)
add(C,T,"Steel Drum","Warm steel-pan tone.","steel drum pan warm",osc1=SIN,osc2=SIN,semi=12,detune=8,mix=40,cutoff=5000,reso=0,fenv=0,atk=1,dec=700,sus=0,rel=500,chorus=15,revmix=25)
# ---- FX ----
C="FX"; T="FX"
add(C,T,"Riser","Long rising sweep for transitions.","riser sweep transition",osc1=SAW,osc2=SAW,detune=40,cutoff=300,reso=40,fenv=100,atk=4000,dec=500,sus=100,rel=1500,revmix=40)
add(C,T,"Laser","Falling laser zap.","laser zap falling",osc1=SAW,osc2=SQ,cutoff=6000,reso=50,fenv=-90,atk=1,dec=200,sus=0,rel=100,dlymix=25)
add(C,T,"Alien Wobble","Warbling alien pitch modulation.","alien wobble warble",osc1=SQ,osc2=SQ,semi=5,cutoff=3000,reso=15,fenv=0,atk=100,dec=300,sus=90,rel=600,lfoshape=0,lforate=12,lfodepth=80,lfodest=PITCH,revmix=40)
add(C,T,"S&H Wind","Windy random filter movement.","wind random sample hold",osc1=SAW,osc2=SAW,detune=40,ftype=BP,cutoff=1500,reso=60,fenv=0,atk=1500,dec=500,sus=100,rel=2000,lfoshape=SH,lforate=4,lfodepth=70,lfodest=CUT,revmix=60)
add(C,T,"Impact Boom","Low impact with a long tail.","impact boom low",osc1=SIN,osc2=SIN,semi=-12,cutoff=300,reso=0,fenv=0,atk=1,dec=1500,sus=0,rel=1500,revmix=60)
add(C,T,"Radio Static","Stuttering radio-style noise-like texture.","radio static stutter",osc1=SQ,osc2=SQ,semi=7,detune=40,ftype=BP,cutoff=2000,reso=80,fenv=0,atk=5,dec=200,sus=80,rel=200,lfoshape=SH,lforate=18,lfodepth=90,lfodest=CUT)
# ---- Cinematic ----
C="Cinematic"; T="CIN"
add(C,T,"Dark Swell","Slow dark swell for scenes.","dark swell slow",osc1=SAW,osc2=SQ,semi=-12,detune=15,cutoff=400,reso=25,fenv=80,atk=3000,dec=500,sus=100,rel=3000,chorus=30,revmix=80,revsize=90)
add(C,T,"Tension Pulse","Pulsing low tension bed.","tension pulse low",osc1=SAW,osc2=SAW,detune=20,cutoff=900,reso=40,fenv=0,atk=200,dec=500,sus=100,rel=800,lfoshape=2,lforate=4,lfodepth=80,lfodest=AMP,revmix=50)
add(C,T,"Hall Hit","Deep hit swallowed by a huge hall.","hit hall deep",osc1=SAW,osc2=SQ,semi=-12,cutoff=1200,reso=15,fenv=70,atk=1,dec=2000,sus=0,rel=2500,revmix=85,revsize=95)
add(C,T,"Ghost Choir","Ghostly choir with a slow vibrato.","ghost choir eerie",osc1=TRI,osc2=SIN,semi=12,ftype=BP,cutoff=1400,reso=45,fenv=10,atk=1500,dec=500,sus=100,rel=2500,lfodepth=10,lforate=4,lfodest=PITCH,chorus=60,revmix=80)
add(C,T,"Stinger Arp","Short, dark arpeggio stinger with long echoes.","stinger arp dark",arpon=1,arpmode=UP,arprate=R116,arpgate=40,arpoct=2,osc1=SAW,osc2=SAW,cutoff=700,reso=25,fenv=60,atk=1,dec=120,sus=0,rel=100,dlymix=40,dlytime=300,dlyfb=50,revmix=60)
add(C,T,"Distant Horns","Far-away horns in a big space.","distant horns epic",osc1=SAW,osc2=SAW,detune=8,cutoff=800,reso=5,fenv=40,atk=400,dec=500,sus=80,rel=1500,chorus=25,revmix=80)
# ---- Hip-Hop ----
C="Hip-Hop"; T="HIP"
add(C,T,"Dusty Keys","Tape-wobbled dusty keys.","dusty tape lofi",osc1=TRI,osc2=SIN,semi=12,mix=30,cutoff=1500,reso=5,fenv=20,atk=1,dec=500,sus=30,rel=300,lfodepth=6,lforate=0.8,lfodest=PITCH,chorus=30,revmix=15)
add(C,T,"Boom Bap Bass","Round, short boom-bap bass.","boom bap round",osc1=SIN,osc2=SQ,semi=-12,mix=30,cutoff=500,reso=10,fenv=40,atk=1,dec=350,sus=60,rel=120)
add(C,T,"Vinyl Pad","Warbly vinyl-style pad.","vinyl warble pad",osc1=SAW,osc2=TRI,detune=15,cutoff=1100,reso=10,fenv=10,atk=300,dec=500,sus=90,rel=800,lfodepth=10,lforate=0.5,lfodest=PITCH,chorus=45,revmix=30)
add(C,T,"Soul Stab","Short soulful stab.","soul stab short",osc1=SAW,osc2=SAW,detune=10,cutoff=1800,reso=10,fenv=60,atk=8,dec=250,sus=10,rel=150,chorus=15,revmix=20)
add(C,T,"Lo-Fi Rhodes","Muffled, wobbling lo-fi electric piano.","lofi rhodes muffled",osc1=SIN,osc2=TRI,semi=12,mix=25,cutoff=1800,reso=5,fenv=20,atk=1,dec=900,sus=25,rel=400,lfodepth=8,lforate=0.6,lfodest=PITCH,chorus=35,revmix=15)
add(C,T,"Chopped Strings","Clipped string stab for sample-style chops.","chopped strings short",osc1=SAW,osc2=SAW,semi=12,cutoff=2000,reso=10,fenv=30,atk=5,dec=120,sus=10,rel=80,dlymix=10)
# ---- R&B ----
C="R&B"; T="RNB"
add(C,T,"Silk Pad","Smooth silky pad.","silk smooth pad",osc1=TRI,osc2=SAW,detune=10,cutoff=2000,reso=5,fenv=15,atk=600,dec=500,sus=95,rel=1500,chorus=60,revmix=50)
add(C,T,"Velvet EP","Smooth EP with tremolo.","velvet smooth tremolo",osc1=SIN,osc2=TRI,semi=12,mix=35,cutoff=3000,reso=5,fenv=30,atk=1,dec=1000,sus=40,rel=600,lfodepth=20,lforate=4.5,lfodest=AMP,chorus=50,revmix=35)
add(C,T,"Smooth Bass","Soft round bass with a slow bloom.","smooth round soft",osc1=SIN,osc2=TRI,semi=-12,mix=40,cutoff=700,reso=15,fenv=30,atk=5,dec=300,sus=80,rel=200)
add(C,T,"Gospel Organ","Bright gospel organ with a fast rotor.","gospel organ bright",osc1=SQ,osc2=SQ,semi=12,detune=3,cutoff=7000,reso=0,fenv=0,atk=4,dec=10,sus=100,rel=100,lfodepth=6,lforate=6,lfodest=PITCH,chorus=45,revmix=25)
add(C,T,"Falsetto Lead","High, airy lead with vibrato and echo.","falsetto airy vibrato",osc1=SIN,osc2=TRI,semi=12,mix=40,cutoff=5000,reso=0,fenv=10,atk=60,dec=300,sus=90,rel=400,lfodepth=14,lforate=5,lfodest=PITCH,dlymix=25,revmix=35)
add(C,T,"Bedroom Keys","Intimate keys with chorus and reverb.","bedroom intimate keys",osc1=TRI,osc2=SAW,semi=12,mix=30,cutoff=2200,reso=5,fenv=25,atk=1,dec=700,sus=30,rel=500,chorus=45,revmix=40)
# ---- Trap ----
C="Trap"; T="TRP"
add(C,T,"808 Sub","Pure long sub for trap low end.","808 sub long",osc1=SIN,osc2=SIN,mix=0,cutoff=300,reso=0,fenv=0,atk=1,dec=1500,sus=30,rel=300)
add(C,T,"Trap Bell","Cold, bright bell loop sound.","trap bell cold",osc1=SIN,osc2=SIN,semi=12,detune=30,mix=25,cutoff=7000,reso=0,fenv=0,atk=1,dec=1200,sus=0,rel=900,revmix=45,dlymix=25)
add(C,T,"Dark Pluck","Dark short pluck with echo.","dark pluck echo",osc1=SAW,osc2=SQ,cutoff=900,reso=35,fenv=70,atk=1,dec=200,sus=0,rel=150,revmix=40,dlymix=30)
add(C,T,"Hat Roll Arp","Fast high arpeggio that rolls like hi-hats.","hat roll arp fast",arpon=1,arpmode=UP,arprate=R132,arpgate=30,arpoct=2,osc1=SQ,osc2=SQ,semi=12,ftype=HP,cutoff=5000,reso=10,fenv=0,atk=1,dec=40,sus=0,rel=30)
add(C,T,"Trap Lead","Lead with a long dotted-style echo.","trap lead echo",osc1=SAW,osc2=SQ,semi=-12,detune=6,cutoff=3500,reso=20,fenv=30,atk=3,dec=250,sus=85,rel=200,dlymix=35,dlytime=375,dlyfb=45,revmix=25)
drum(C,T,"Trap Kit","Low-tuned kit with a medium room.","trap low kit",drumtune=-4,drumdecay=150,revmix=12)
# ---- Dance ----
C="Dance"; T="DNC"
add(C,T,"Pluck Arp","Classic arpeggiated pluck with echoes.","pluck arp echo",arpon=1,arpmode=UP,arprate=R116,arpgate=50,arpoct=2,osc1=SAW,osc2=SAW,detune=10,cutoff=2200,reso=30,fenv=50,atk=1,dec=120,sus=0,rel=100,dlymix=30,dlytime=375,dlyfb=40,revmix=20)
add(C,T,"Supersaw Chord","Very wide saw chord stack.","supersaw wide chord",osc1=SAW,osc2=SAW,detune=28,cutoff=9000,reso=5,fenv=10,atk=3,dec=300,sus=100,rel=250,chorus=60,revmix=25)
add(C,T,"Rave Stab","Short detuned rave stab.","rave stab short",osc1=SAW,osc2=SQ,semi=7,detune=12,cutoff=3000,reso=20,fenv=55,atk=1,dec=200,sus=0,rel=120,revmix=20,dlymix=20)
add(C,T,"Trance Gate","Rhythmic gated chord.","trance gate rhythmic",osc1=SAW,osc2=SAW,detune=15,cutoff=4000,reso=10,fenv=0,atk=3,dec=300,sus=100,rel=100,lfoshape=2,lforate=8,lfodepth=100,lfodest=AMP,dlymix=30,dlytime=375,dlyfb=45,revmix=25)
add(C,T,"Pumping Pad","Pad with a slow pulsing volume.","pumping pad sidechain",osc1=SAW,osc2=TRI,semi=12,cutoff=2500,reso=10,fenv=0,atk=100,dec=500,sus=100,rel=800,lfoshape=1,lforate=2,lfodepth=60,lfodest=AMP,revmix=40,chorus=40)
add(C,T,"Acid Arp","Acid-style squelchy arpeggio.","acid arp squelch",arpon=1,arpmode=UPDN,arprate=R116,arpgate=60,arpoct=1,osc1=SAW,mix=0,cutoff=600,reso=70,fenv=85,atk=1,dec=150,sus=0,rel=80,dlymix=30)

# ---- level matching (filled in by the calibrate step) ----
gp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "preset_gain.json")
gain = json.load(open(gp)) if os.path.exists(gp) else {}
for p in P:
    if p["name"] in gain: p["v"]["vol"] = max(-40.0, min(6.0, gain[p["name"]]))
assert len(P) >= 100, len(P)
names = [p["name"] for p in P]; assert len(set(names)) == len(names)
with open(os.path.join(plug, "src/Presets.inc"), "w") as f:
    f.write("// Generated by tools/make_presets.py. Do not edit; edit the script and regenerate.\n")
    f.write("struct PresetDef { const char* name; float v[%d]; };\nstatic const PresetDef kPresets[] = {\n" % len(ORDER))
    for p in P:
        f.write('    {"%s", {%s}},\n' % (p["name"], ", ".join(("%.6g" % float(p["v"][k])) + ("" if any(c in ("%.6g" % float(p["v"][k])) for c in ".e") else ".0") + "f" for k in ORDER)))
    f.write("};\n")
json.dump([dict(index=8 + i, name=p["name"], category=p["category"], description=p["description"], tags=p["tags"],
                params={k: p["v"][k] for k in ORDER}) for i, p in enumerate(P)], open(os.path.join(plug, "presets.json"), "w"), indent=1)
from collections import Counter
print(len(P), "presets;", dict(Counter(p["category"] for p in P)))

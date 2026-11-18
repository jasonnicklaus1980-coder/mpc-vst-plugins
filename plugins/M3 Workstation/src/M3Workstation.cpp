// M3 Workstation - an MPC instrument plugin by Shannon
//
// Stage 3 milestone: a playable 6-voice, two-oscillator subtractive synth with a
// resonant state-variable lowpass, filter envelope, amp envelope and five programs.
// The Korg M3 architecture (multi-part combinations, drum kits, sample playback,
// effects, arpeggiator) is built in later stages.
//
// Signal path per voice:
//   osc1 (PolyBLEP) x (1-mix) + osc2 (PolyBLEP, semi + detune) x mix
//   -> SVF lowpass (cutoff + filter env, resonance)  -> amp envelope * velocity
//   -> voice sum -> master volume -> both outputs.
//
// Rules:
//   * process() runs on the audio thread: no allocation, locks, logging or file I/O.
//   * Parameter 0 is nudged by the MPC on every prepare, so it is Volume (no side effects).
//   * Block sizes are maximums; real blocks are 32..512 frames.
//   * Parameter ids and the uid are permanent once presets exist.

#include "mpc_plugin.h"
#include "dsp/oscillator.h"
#include "dsp/svf.h"
#include "SampleBank.h"
#include "Effects.h"
#include "Arpeggiator.h"
#include "DrumSynth.h"
#include "Presets.inc"

#include <cmath>
#include <sstream>
#include <string>
#include <deque>

using mpc::dsp::PolyBlepOsc;
using mpc::dsp::SVF;

namespace {

// Parameter order must match the addParam() calls in the constructor.
enum ParamIndex
{
    kVolume = 0,   // dB (index 0 is nudged by the host, so it must be harmless)
    kOsc1Wave,     // choice: Sine / Triangle / Saw / Square
    kOsc2Wave,     // choice
    kOsc2Semi,     // -12..12 semitones
    kOsc2Detune,   // cents
    kMix,          // 0..100 %: 0 = osc1 only, 100 = osc2 only
    kCutoff,       // Hz
    kReso,         // 0..100 %
    kFilterEnv,    // -100..100 %: how far the filter envelope opens the cutoff (up to 4 octaves)
    kAttack,       // ms
    kDecay,        // ms
    kSustain,      // 0..100 %
    kRelease,      // ms
    kSource,       // choice: Synth / Sample
    kInstrument,   // 1..32: which loaded multisample instrument (Sample source), in Content/Instruments/index.txt order
    kLoop,         // bool: use the sample's loop points
    kFilterType,   // choice: Lowpass / Highpass / Bandpass / Notch
    kKeyTrack,     // 0..100 %: cutoff follows the key (100 % = one octave per octave, around middle C)
    kLfoShape,     // choice: Sine / Triangle / Square / Sample&Hold
    kLfoRate,      // Hz
    kLfoDepth,     // 0..100 % (the mod wheel, CC1, adds to this)
    kLfoDest,      // choice: Pitch (+/-2 st at full depth) / Cutoff (+/-2 oct) / Amp (tremolo)
    kLfoOn,        // bool: LFO on/off
    kChorusMix,    // 0..100 %
    kDelayMix,     // 0..100 %
    kDelayTime,    // ms
    kDelayFb,      // 0..90 % feedback (repeats cross between L and R)
    kRevMix,       // 0..100 %
    kRevSize,      // 0..100 % room size / decay
    kArpOn,        // bool
    kArpMode,      // choice: Up / Down / Up-Down / Random / Order / Chord
    kArpRate,      // choice: 1/4 1/8 1/8T 1/16 1/16T 1/32 (tempo synced)
    kArpGate,      // 10..100 % of the step
    kArpOct,       // 1..4 octaves
    kArpSwing,     // 0..50 %
    kArpLatch,     // bool: keep the chord after the keys are released
    kDrumTune,     // -12..12 semitones, all drums
    kDrumDecay,    // 50..200 %, all drums
    kDrumKit,      // choice: Built-in synth kit / Sample (the selected instrument's key map, one-shot)
    kMode,         // choice: Program (the knobs) / Combi (layers and splits of several programs)
    kCombi,        // choice: which combination, used when Mode = Combi
    kNumParams
};

constexpr int   kNumVoices   = 12;
constexpr int   kCtrlInterval = 32;    // filter coefficients update every N samples
constexpr float kPi           = 3.14159265358979f;

// Attack / decay / sustain / release with exponential-style segments.
struct Envelope
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };

    Stage stage = Idle;
    float level = 0.0f;
    float attackRate = 0.0f, decayRate = 0.0f, releaseRate = 0.0f, sustainLevel = 1.0f;

    void setTimes(float attackMs, float decayMs, float sustainPercent, float releaseMs, float sampleRate)
    {
        // Decay and release are exponential segments. The time knobs mean "time to settle"
        // (to within ~0.001 of the target), not the time constant, so scale the rate by the
        // number of time constants needed to get there.
        sustainLevel = mpc::clamp(sustainPercent * 0.01f, 0.0f, 1.0f);
        const float decayConstants   = std::fmax(0.7f, std::log((1.0f - sustainLevel + 0.02f) / 0.021f));
        const float releaseConstants = 3.9f;   // 1.0 -> 0.0005 with the 0.02 overshoot target
        attackRate   = 1.0f / std::fmax(1.0f, attackMs  * 0.001f * sampleRate);
        decayRate    = decayConstants   / std::fmax(1.0f, decayMs   * 0.001f * sampleRate);
        releaseRate  = releaseConstants / std::fmax(1.0f, releaseMs * 0.001f * sampleRate);
    }

    void noteOn()  { stage = Attack; }
    void noteOff() { if (stage != Idle) stage = Release; }
    bool isActive() const { return stage != Idle; }

    float next()
    {
        switch (stage)
        {
            case Attack:  level += attackRate; if (level >= 1.0f) { level = 1.0f; stage = Decay; } break;
            case Decay:   level -= decayRate * (level - sustainLevel + 0.02f); if (level <= sustainLevel + 0.001f) { level = sustainLevel; stage = Sustain; } break;
            case Sustain: level = sustainLevel; break;
            case Release: level -= releaseRate * (level + 0.02f); if (level <= 0.0005f) { level = 0.0f; stage = Idle; } break;
            case Idle:    level = 0.0f; break;
        }
        return level;
    }
};

// Everything a voice needs from a program. Program mode reads it live from the knobs; Combi mode freezes a copy
// of the part's program into the voice at note-on, so several programs can sound at once.
struct Patch
{
    int   wave1 = 2, wave2 = 2;
    float osc2Ratio = 1.0f, gain1 = 0.5f, gain2 = 0.5f;
    float cutoff = 3000.0f, reso = 0.2f, fenv = 0.4f, keyTrack = 0.0f;
    float atk = 5.0f, dec = 250.0f, sus = 70.0f, rel = 300.0f;
    SVF::Mode fmode = SVF::Mode::Lowpass;
    bool  loop = true;
    int   source = 0, instrument = 1, drumKit = 0;
    float drumTune = 0.0f, drumDecay = 1.0f;
};

// One part of a combination: a program played over a key range and velocity range.
struct Part { int program; int lo, hi, vlo, vhi, transpose; float level, pan; };
struct Combi { const char* name; int numParts; Part parts[4]; };

// Program indices: 0 Init, 1 Pad, 2 Pluck, 3 Bass, 4 Sample Pad, 5 Lead, 6 Arp Lead, 7 Drum Kit
const Combi kCombis[] = {
    {"Pad + Pluck",    2, {{1, 0, 127, 1, 127, 0, 0.9f, -0.2f}, {2, 0, 127, 1, 127, 0, 0.7f, 0.2f}}},
    {"Bass / Lead",    2, {{3, 0, 59, 1, 127, 0, 1.0f, 0.0f},   {5, 60, 127, 1, 127, 0, 0.9f, 0.0f}}},
    {"Velocity Switch", 2, {{2, 0, 127, 1, 90, 0, 1.0f, 0.0f},  {5, 0, 127, 91, 127, 0, 0.9f, 0.0f}}},
    {"Layer Stack",    3, {{1, 0, 127, 1, 127, 0, 0.8f, -0.3f}, {2, 0, 127, 1, 127, 0, 0.6f, 0.0f}, {5, 0, 127, 1, 127, 12, 0.5f, 0.3f}}},
    {"Drums / Lead",   2, {{7, 0, 59, 1, 127, 0, 1.0f, 0.0f},   {5, 60, 127, 1, 127, 0, 0.9f, 0.0f}}},
    {"Sample + Pad",   2, {{4, 0, 127, 1, 127, 0, 0.8f, 0.0f},  {1, 0, 127, 1, 127, 0, 0.8f, 0.0f}}},
    {"Octave Bass",    2, {{3, 0, 127, 1, 127, 0, 1.0f, 0.0f},  {3, 0, 127, 1, 127, 12, 0.5f, 0.0f}}},
};
constexpr int kNumCombis = (int)(sizeof(kCombis) / sizeof(kCombis[0]));

// Builds a Patch from plain parameter values; `val(index)` returns the value of parameter `index`.
template <class F>
Patch buildPatch(F val)
{
    static const SVF::Mode kModes[4] = {SVF::Mode::Lowpass, SVF::Mode::Highpass, SVF::Mode::Bandpass, SVF::Mode::Notch};
    Patch p;
    p.wave1 = (int)std::lround(val(kOsc1Wave)); p.wave2 = (int)std::lround(val(kOsc2Wave));
    p.osc2Ratio = std::pow(2.0f, (val(kOsc2Semi) + val(kOsc2Detune) * 0.01f) / 12.0f);
    p.gain2 = mpc::clamp(val(kMix) * 0.01f, 0.0f, 1.0f); p.gain1 = 1.0f - p.gain2;
    p.cutoff = val(kCutoff); p.reso = mpc::clamp(val(kReso) * 0.01f, 0.0f, 1.0f); p.fenv = val(kFilterEnv) * 0.01f;
    p.keyTrack = val(kKeyTrack) * 0.01f;
    p.atk = val(kAttack); p.dec = val(kDecay); p.sus = val(kSustain); p.rel = val(kRelease);
    p.fmode = kModes[mpc::clamp((int)std::lround(val(kFilterType)), 0, 3)];
    p.loop = val(kLoop) >= 0.5f;
    p.source = (int)std::lround(val(kSource)); p.instrument = (int)std::lround(val(kInstrument));
    p.drumKit = (int)std::lround(val(kDrumKit));
    p.drumTune = val(kDrumTune); p.drumDecay = val(kDrumDecay) * 0.01f;
    return p;
}

// One voice: two oscillators, a filter, an amp envelope and a filter envelope.
struct Voice
{
    Patch    p;                   // used when `frozen` (Combi mode); Program mode reads the live knobs instead
    bool     frozen = false;
    int      inNote = -1;         // the key that started this voice (for note-off), before part transpose
    float    level = 1.0f;        // part level
    int      note = -1;
    float    velocity = 0.0f;
    float    baseHz = 440.0f;
    PolyBlepOsc osc1, osc2;
    SVF      filter;
    Envelope ampEnv, fltEnv;
    uint32_t startOrder = 0;      // for voice stealing (oldest first)
    const m3::Region* region = nullptr;   // non-null = sample voice
    double   pos = 0.0, inc = 0.0;        // sample read position and increment (frames)
    double   baseInc = 0.0;               // increment before pitch bend
    float    panL = 1.0f, panR = 1.0f;
    bool     oneShot = false;               // Drum mode: ignore note-off, never loop

    bool isActive() const { return ampEnv.isActive(); }

    // Clears musical state but keeps the sample rate that prepare() set.
    void silence()
    {
        note = inNote = -1;
        frozen = false;
        level = 1.0f;
        velocity = 0.0f;
        region = nullptr;
        pos = inc = baseInc = 0.0;
        oneShot = false;
        panL = panR = 1.0f;
        ampEnv = Envelope();
        fltEnv = Envelope();
        filter.reset();
        osc1.resetPhase();
        osc2.resetPhase();
    }
};

} // namespace

class M3Workstation : public mpc::Plugin
{
public:
    M3Workstation() : mpc::Plugin(makeInfo())
    {
        // Names are what the MPC shows (keep them short). Ids are saved in presets.
        addParam(mpc::floatParam("volume",  "Volume",     -40.0f, 6.0f,   -6.0f,   "dB", 1));
        addParam(mpc::choiceParam("osc1",   "Osc1 Wave",  {"Sine", "Triangle", "Saw", "Square"}, 2));
        addParam(mpc::choiceParam("osc2",   "Osc2 Wave",  {"Sine", "Triangle", "Saw", "Square"}, 2));
        addParam(mpc::intParam  ("semi",    "Osc2 Semi",  -12, 12, 0, "st"));
        addParam(mpc::floatParam("detune",  "Osc2 Detune", -50.0f, 50.0f, 7.0f, "ct", 0));
        addParam(mpc::floatParam("mix",     "Osc Mix",    0.0f, 100.0f, 50.0f, "%", 0));
        addParam(mpc::logParam  ("cutoff",  "Cutoff",     20.0f, 18000.0f, 3000.0f, "Hz", 0));
        addParam(mpc::floatParam("reso",    "Resonance",  0.0f, 100.0f, 20.0f, "%", 0));
        addParam(mpc::floatParam("fenv",    "Filter Env", -100.0f, 100.0f, 40.0f, "%", 0));
        addParam(mpc::logParam  ("attack",  "Attack",     1.0f, 5000.0f, 5.0f, "ms", 0));
        addParam(mpc::logParam  ("decay",   "Decay",      1.0f, 5000.0f, 250.0f, "ms", 0));
        addParam(mpc::floatParam("sustain", "Sustain",    0.0f, 100.0f, 70.0f, "%", 0));
        addParam(mpc::logParam  ("release", "Release",    1.0f, 5000.0f, 300.0f, "ms", 0));
        addParam(mpc::choiceParam("source", "Source",     {"Synth", "Sample", "Drum"}, 0));
        addParam(mpc::intParam  ("inst",    "Instrument", 1, 32, 1, ""));
        addParam(mpc::boolParam ("loop",    "Loop", true));
        addParam(mpc::choiceParam("ftype",  "Filter Type", {"Lowpass", "Highpass", "Bandpass", "Notch"}, 0));
        addParam(mpc::floatParam("ktrack",  "Key Track",  0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::choiceParam("lfoshape", "LFO Shape", {"Sine", "Triangle", "Square", "S&H"}, 0));
        addParam(mpc::logParam  ("lforate", "LFO Rate",   0.1f, 20.0f, 5.0f, "Hz", 1));
        addParam(mpc::floatParam("lfodepth", "LFO Depth", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::choiceParam("lfodest", "LFO Dest",  {"Pitch", "Cutoff", "Amp"}, 0));
        addParam(mpc::boolParam ("lfoon",   "LFO On", true));
        addParam(mpc::floatParam("chomix",  "Chorus Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::floatParam("dlymix",  "Delay Mix",  0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::floatParam("dlytime", "Delay Time", 10.0f, 1000.0f, 350.0f, "ms", 0));
        addParam(mpc::floatParam("dlyfb",   "Delay Feedback", 0.0f, 90.0f, 35.0f, "%", 0));
        addParam(mpc::floatParam("revmix",  "Reverb Mix", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::floatParam("revsize", "Reverb Size", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(mpc::boolParam ("arpon",   "Arp On", false));
        addParam(mpc::choiceParam("arpmode", "Arp Mode", {"Up", "Down", "Up/Down", "Random", "Order", "Chord"}, 0));
        addParam(mpc::choiceParam("arprate", "Arp Rate", {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"}, 3));
        addParam(mpc::floatParam("arpgate", "Arp Gate", 10.0f, 100.0f, 80.0f, "%", 0));
        addParam(mpc::intParam  ("arpoct",  "Arp Octaves", 1, 4, 1, ""));
        addParam(mpc::floatParam("arpswing", "Arp Swing", 0.0f, 50.0f, 0.0f, "%", 0));
        addParam(mpc::boolParam ("arplatch", "Arp Latch", false));
        addParam(mpc::floatParam("drumtune", "Drum Tune",  -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(mpc::floatParam("drumdecay", "Drum Decay", 50.0f, 200.0f, 100.0f, "%", 0));
        addParam(mpc::choiceParam("drumkit", "Drum Kit", {"Built-in", "Sample"}, 0));
        addParam(mpc::choiceParam("mode",   "Mode",       {"Program", "Combi"}, 0));
        {
            std::vector<std::string> names;
            for (const Combi& c : kCombis) names.push_back(c.name);
            addParam(mpc::choiceParam("combi", "Combination", names, 0));
        }

        // Programs: plain values in parameter order.
        //         vol   osc1 osc2 semi det  mix  cut   res  fenv  atk  dec   sus  rel src inst loop | ftype ktrack lfoshape rate depth dest
        addProg("Init",  {-6.0f, 2, 2,   0,  7, 50, 3000, 20,  40,   5, 250,  70, 300, 0, 1, 1, 0, 0, 0, 5, 0, 0, 1, 0, 0, 350, 35, 0, 50, 0, 0, 3, 80, 1, 0, 0, 0, 100, 0, 0, 0});
        addProg("Pad",   {-8.0f, 1, 2,   0, 12, 50, 1500, 10,  30, 400, 1000,  80, 900, 0, 1, 1, 0, 30, 0, 0.6f, 25, 1, 1, 35, 0, 450, 30, 30, 70, 0, 0, 3, 80, 1, 0, 0, 0, 100, 0, 0, 0});
        addProg("Pluck", {-6.0f, 2, 3,   0,  4, 50, 5000, 30,  70,   1,  150,   0, 150, 0, 1, 1, 0, 0, 0, 5, 0, 0, 1, 0, 18, 300, 35, 12, 40, 0, 0, 3, 80, 1, 0, 0, 0, 100, 0, 0, 0});
        addProg("Bass",  {-4.0f, 2, 2, -12,  5, 50,  800, 35,  50,   2,  300,  60, 120, 0, 1, 1, 0, 0, 0, 5, 0, 0, 1, 0, 0, 350, 35, 0, 50, 0, 0, 3, 80, 1, 0, 0, 0, 100, 0, 0, 0});
        addProg("Sample Pad", {-6.0f, 2, 2, 0, 0, 0, 18000, 0, 0, 300, 500, 100, 800, 1, 1, 1, 0, 0, 0, 5, 0, 0, 1, 0, 0, 350, 35, 10, 60, 0, 0, 3, 80, 1, 0, 0, 0, 100, 0, 0, 0});
        addProg("Lead",  {-6.0f, 2, 2,  12,  7, 50, 4000, 45,  35,  10,  200,  80, 250, 0, 1, 1, 0, 40, 0, 5.5f, 12, 0, 1, 0, 22, 375, 40, 15, 50, 0, 0, 3, 80, 1, 0, 0, 0, 100, 0, 0, 0});

        addProg("Arp Lead", {-6.0f, 2, 2, 12, 7, 50, 3500, 40, 45, 2, 180, 0, 160, 0, 1, 1, 0, 25, 0, 5, 0, 0, 1, 0, 22, 330, 40, 15, 45, 1, 0, 3, 70, 2, 0, 0, 0, 100, 0, 0, 0});

        addProg("Drum Kit", {-4.0f, 2, 2, 0, 0, 0, 18000, 0, 0, 1, 250, 100, 300, 2, 1, 0, 0, 0, 0, 5, 0, 0, 1, 0, 0, 350, 35, 0, 50, 0, 0, 3, 80, 1, 0, 0, 0, 100, 0, 0, 0});

        // Factory preset bank (generated table, original sounds): programs 8+.
        for (const PresetDef& pd : kPresets)
            addProg(pd.name, std::vector<float>(pd.v, pd.v + kNumParams));

        // User presets: Content/Presets/user.m3p, one "name|41 values" line each (made by tools/triton_to_m3.py).
        // Constructor only (reads a file). Bad lines are skipped, never guessed at.
        {
            std::string text;
            if (mpc::readTextFile(mpc::contentPath("Presets/user.m3p"), text))
            {
                std::istringstream in(text);
                std::string line;
                int added = 0;
                while (added < 4096 && std::getline(in, line))
                {
                    const size_t bar = line.find('|');
                    if (bar == std::string::npos || bar == 0) continue;
                    std::istringstream nums(line.substr(bar + 1));
                    std::vector<float> vals;
                    float x;
                    while (nums >> x) vals.push_back(x);
                    if ((int)vals.size() != (int)kNumParams) continue;
                    bool ok = true;
                    for (float f : vals) ok = ok && std::isfinite(f);
                    if (!ok) continue;
                    userNames_.push_back(line.substr(0, std::min<size_t>(bar, 24)));
                    addProg(userNames_.back().c_str(), vals);
                    ++added;
                }
            }
        }

        // Multisample instruments from Content/Instruments (not realtime: constructor only).
        bank_.load();
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        sampleRate_ = (float)sampleRate;
        for (Voice& v : voices_)
        {
            v.osc1.setSampleRate(sampleRate);
            v.osc2.setSampleRate(sampleRate);
            v.filter.setSampleRate(sampleRate);
        }
        fx_.init(sampleRate_);   // allocates: prepare() is not realtime
        reset();
    }

    void reset() override
    {
        for (Voice& v : voices_) v.silence();
        fx_.clear();
        arp_.reset(); arpWasOn_ = false;
        for (m3::DrumVoice& d : drums_) d.active = false;
        noteCounter_ = 0;
        ctrlCountdown_ = 0;
        smoothedGain_ = dbToGain(get(kVolume));
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int numFrames = ctx.numFrames;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;

        // Block-rate parameter reads (cheap atomics, plain values).
        const float targetGain = dbToGain(get(kVolume));
        live_ = buildPatch([this](int i) { return get(i); });   // Program mode: the knobs, read once per block
        const int   lfoShape = getInt(kLfoShape), lfoDest = getInt(kLfoDest);
        const float lfoRate  = get(kLfoRate);
        const float chMix = get(kChorusMix) * 0.01f, dlMix = get(kDelayMix) * 0.01f, dlTime = get(kDelayTime) * 0.001f;
        const float dlFb = get(kDelayFb) * 0.01f, rvMix = get(kRevMix) * 0.01f, rvSize = get(kRevSize) * 0.01f;
        const float lfoDepth = getBool(kLfoOn) ? mpc::clamp(get(kLfoDepth) * 0.01f + modWheel_, 0.0f, 1.0f) : 0.0f;

        for (Voice& v : voices_)
        {
            const Patch& pp = patchOf(v);
            v.ampEnv.setTimes(pp.atk, pp.dec, pp.sus, pp.rel, sampleRate_);
            v.fltEnv.setTimes(pp.atk, pp.dec, pp.sus, pp.rel, sampleRate_);
            v.osc1.setWave(pp.wave1);
            v.osc2.setWave(pp.wave2);
            if (v.isActive() && !v.region)
            {
                v.osc1.setFrequency(v.baseHz * pitchRatio());
                v.osc2.setFrequency(v.baseHz * pp.osc2Ratio * pitchRatio());
            }
        }

        // Arpeggiator: note events feed the arp, which returns timed note events; other MIDI passes through.
        const mpc::MidiEvent* midiIn = ctx.midi;
        int numMidi = ctx.numMidiEvents;
        const bool arpOn = getBool(kArpOn);
        if (arpOn || arpWasOn_)
        {
            m3::ArpSettings as;
            as.mode = getInt(kArpMode); as.rate = getInt(kArpRate); as.octaves = getInt(kArpOct);
            as.gate = get(kArpGate) * 0.01f; as.swing = get(kArpSwing) * 0.01f; as.latch = getBool(kArpLatch);
            int np = 0;
            for (int k = 0; k < ctx.numMidiEvents; ++k)
            {
                const mpc::MidiEvent& e = ctx.midi[k];
                if (arpOn && e.isNoteOn()) arp_.noteOn(e.note(), e.velocity());
                else if (arpOn && e.isNoteOff()) arp_.noteOff(e.note());
                else if (np < kMaxMerged) merged_[np++] = e;
            }
            int na = 0;
            if (arpOn) na = arp_.process(numFrames, ctx.transport.tempoValid ? ctx.transport.bpm : 120.0, sampleRate_, as, arpOut_);
            else { arp_.clearHeld(); na = arp_.process(numFrames, 120.0, sampleRate_, as, arpOut_); }   // arp just switched off: release its notes
            // merge by sample offset (both lists are sorted); the arp's events win ties
            mpc::MidiEvent tmp[kMaxMerged]; int nt = 0, a = 0, b = 0;
            while ((a < na || b < np) && nt < kMaxMerged)
                tmp[nt++] = (b >= np || (a < na && arpOut_[a].sampleOffset <= merged_[b].sampleOffset)) ? arpOut_[a++] : merged_[b++];
            std::copy(tmp, tmp + nt, merged_);
            midiIn = merged_; numMidi = nt;
            arpWasOn_ = arpOn;
        }

        // Walk the block, applying MIDI at its exact sample offset.
        int frame = 0;
        int midiIndex = 0;
        while (frame < numFrames)
        {
            while (midiIndex < numMidi && midiIn[midiIndex].sampleOffset <= frame)
            {
                handleMidi(midiIn[midiIndex]);
                ++midiIndex;
            }
            int segmentEnd = numFrames;
            if (midiIndex < numMidi)
                segmentEnd = mpc::clamp(midiIn[midiIndex].sampleOffset, frame + 1, numFrames);

            for (int i = frame; i < segmentEnd; ++i)
            {
                smoothedGain_ += 0.002f * (targetGain - smoothedGain_);

                // Control rate: update filter coefficients every kCtrlInterval samples.
                if (--ctrlCountdown_ <= 0)
                {
                    ctrlCountdown_ = kCtrlInterval;
                    // Shared free-running LFO, advanced once per control tick.
                    lfoPhase_ += lfoRate * (float)kCtrlInterval / sampleRate_;
                    if (lfoPhase_ >= 1.0f)
                    {
                        lfoPhase_ -= std::floor(lfoPhase_);
                        rng_ = rng_ * 1664525u + 1013904223u;
                        lfoHeld_ = (float)(rng_ >> 8) / 8388608.0f - 1.0f;   // -1..1
                    }
                    switch (lfoShape)
                    {
                        case 0:  lfoVal_ = std::sin(lfoPhase_ * 2.0f * kPi); break;
                        case 1:  lfoVal_ = 1.0f - 4.0f * std::fabs(lfoPhase_ - 0.5f); break;
                        case 2:  lfoVal_ = lfoPhase_ < 0.5f ? 1.0f : -1.0f; break;
                        default: lfoVal_ = lfoHeld_; break;
                    }
                    const float pitchRatio = (lfoDest == 0 && lfoDepth > 0.0f)
                        ? std::pow(2.0f, lfoVal_ * lfoDepth * 2.0f / 12.0f) : 1.0f;
                    const float lfoCutOct = lfoDest == 1 ? lfoVal_ * lfoDepth * 2.0f : 0.0f;
                    lfoAmp_ = lfoDest == 2 ? 1.0f - lfoDepth * (0.5f - 0.5f * lfoVal_) : 1.0f;
                    const bool pitchChanged = pitchRatio != lfoPitchRatio_;
                    lfoPitchRatio_ = pitchRatio;
                    for (Voice& v : voices_)
                    {
                        if (!v.isActive()) continue;
                        if (pitchChanged) applyPitch(v);
                        const Patch& pp = patchOf(v);
                        const float octaves = pp.fenv * v.fltEnv.level * 4.0f
                                            + lfoCutOct + pp.keyTrack * ((float)v.note - 60.0f) / 12.0f;
                        v.filter.set(mpc::clamp(pp.cutoff * std::pow(2.0f, octaves), 20.0f, 18000.0f), pp.reso);
                    }
                }

                float mixL = 0.0f, mixR = 0.0f;
                for (Voice& v : voices_)
                {
                    if (!v.isActive()) continue;
                    const Patch& pp = patchOf(v);
                    float s;
                    if (v.region)
                    {
                        const m3::Region& r = *v.region;
                        const bool looping = pp.loop && r.hasLoop() && !v.oneShot;
                        s = m3::readInterpolated(r, v.pos, looping);
                        v.pos += v.inc;
                        if (looping)
                        {
                            const double span = (double)(r.loopEnd - r.loopStart);
                            while (v.pos >= (double)r.loopEnd) v.pos -= span;
                        }
                        else if (v.pos >= (double)r.data.size())
                        {
                            v.ampEnv = Envelope();   // one-shot sample finished: free the voice
                        }
                    }
                    else
                    {
                        s = v.osc1.process() * pp.gain1 + v.osc2.process() * pp.gain2;
                    }
                    const float lp = v.filter.process(s, pp.fmode);
                    const float amp = lp * v.ampEnv.next() * v.velocity * v.level * lfoAmp_;
                    mixL += amp * v.panL;
                    mixR += amp * v.panR;
                    v.fltEnv.next();
                }
                const float g = smoothedGain_ * 0.3f;   // headroom for up to 6 voices
                float fl = mixL * g, fr = mixR * g;
                float dl = 0.0f, dr = 0.0f;                       // drum bus: follows Volume, soft-limited
                for (m3::DrumVoice& d : drums_) d.process(sampleRate_, dl, dr);
                fl += std::tanh(dl * 0.6f) * smoothedGain_ * 1.2f;
                fr += std::tanh(dr * 0.6f) * smoothedGain_ * 1.2f;
                fx_.chorus.process(fl, fr, chMix);
                fx_.delay.process(fl, fr, dlTime, dlFb, dlMix);
                fx_.reverb.process(fl, fr, rvSize, rvMix);
                outL[i] = fl;
                if (outR) outR[i] = fr;
            }
            frame = segmentEnd;
        }
    }

    void parameterChanged(int index, float plainValue) override
    {
        (void)index; (void)plainValue;   // no side effects (parameter 0 is nudged on load)
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "M3 Workstation";
        info.vendor       = "Shannon";
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);   // must match plugin.json "uid"
        info.version      = 1000;   // 1.0.0.0
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    static constexpr char kUid[5] = "S5OY";

    static float dbToGain(float dB) { return std::pow(10.0f, dB / 20.0f); }

    // Starts one note with a given patch. `frozen` voices keep their own copy of the patch (Combi parts).
    void startNote(int inNote, int note, int vel, const Patch& pt, bool frozen, float level, float pan)
    {
        if (note < 0 || note > 127) return;
        const m3::Region* region = nullptr;
        if (pt.source == 2 && pt.drumKit == 0)                       // built-in synthesized drum kit
        {
            const m3::DrumPad* pad = m3::drumPadFor(note);
            if (!pad) return;
            m3::DrumVoice* dv = &drums_[0];
            for (m3::DrumVoice& d : drums_)
            {
                if (pad->group && d.active && d.group == pad->group) d.choking = true;   // hat choke
                if (!dv->active) continue;
                if (!d.active || d.age < dv->age) dv = &d;
            }
            dv->start(note, *pad, vel, std::pow(2.0f, pt.drumTune / 12.0f), pt.drumDecay, ++noteCounter_, level, pan);
            return;
        }
        if (pt.source == 1 || (pt.source == 2 && pt.drumKit == 1))
        {
            const m3::Instrument* inst = bank_.get(pt.instrument - 1);
            region = inst ? inst->find(note) : nullptr;
            if (!region) return;   // no sample on this key (or no instrument loaded): stay silent
        }
        Voice& v = findFreeVoice();
        v.silence();
        v.p = pt; v.frozen = frozen; v.level = level;
        v.inNote = inNote;
        v.oneShot = (pt.source == 2);
        v.note = note;
        v.velocity = vel / 127.0f;
        v.baseHz = mpc::noteToHz((float)note);
        float panPos = pan;
        if (region)
        {
            v.region = region;
            v.pos = 0.0;
            v.baseInc = (region->rate / (double)sampleRate_) *
                        std::pow(2.0, ((double)note - region->root) / 12.0 + (double)region->tuneCents / 1200.0);
            v.inc = v.baseInc * pitchRatio();
            panPos += ((float)region->pan - 64.0f) / 64.0f;
        }
        panPos = mpc::clamp(panPos, -1.0f, 1.0f);
        const float angle = (panPos + 1.0f) * 0.25f * kPi;            // equal-power pan, centre = unity
        v.panL = std::cos(angle) * 1.41421356f;
        v.panR = std::sin(angle) * 1.41421356f;
        v.osc1.setFrequency(v.baseHz * pitchRatio());
        v.osc2.setFrequency(v.baseHz * pt.osc2Ratio * pitchRatio());
        v.startOrder = ++noteCounter_;
        v.ampEnv.noteOn();
        v.fltEnv.noteOn();
        ctrlCountdown_ = 0;   // refresh filter coefficients on the next sample
    }

    void handleMidi(const mpc::MidiEvent& e)
    {
        if (e.isNoteOn())
        {
            if (getInt(kMode) == 1)                                   // Combi: every part whose ranges match plays
            {
                const Combi& c = kCombis[mpc::clamp(getInt(kCombi), 0, kNumCombis - 1)];
                for (int k = 0; k < c.numParts; ++k)
                {
                    const Part& pt = c.parts[k];
                    if (e.note() < pt.lo || e.note() > pt.hi || e.velocity() < pt.vlo || e.velocity() > pt.vhi) continue;
                    startNote(e.note(), e.note() + pt.transpose, e.velocity(), progPatches_[(size_t)pt.program], true, pt.level, pt.pan);
                }
            }
            else
                startNote(e.note(), e.note(), e.velocity(), live_, false, 1.0f, 0.0f);
        }
        else if (e.isNoteOff())
        {
            for (Voice& v : voices_)
            {
                if (v.inNote == e.note() && v.isActive() && !v.oneShot)
                {
                    v.ampEnv.noteOff();
                    v.fltEnv.noteOff();
                }
            }
        }
        else if (e.isPitchBend())
        {
            // +/-2 semitones. Applied to every sounding voice; new notes pick it up at note-on.
            bendRatio_ = std::pow(2.0f, (float)e.pitchBend() / 8192.0f * 2.0f / 12.0f);
            for (Voice& v : voices_) if (v.isActive()) applyPitch(v);
        }
        else if (e.isController() && e.controller() == 1)
            modWheel_ = (float)e.controllerValue() / 127.0f;
        else if (e.isAllNotesOff())
        {
            for (Voice& v : voices_)
            {
                v.ampEnv.noteOff();
                v.fltEnv.noteOff();
            }
        }
    }

    static constexpr int kMaxMerged = 256;
    m3::Arpeggiator arp_;
    m3::DrumVoice drums_[8];
    bool arpWasOn_ = false;
    mpc::MidiEvent merged_[kMaxMerged];
    mpc::MidiEvent arpOut_[m3::Arpeggiator::kMaxOut];
    m3::MasterFx fx_;
    float bendRatio_ = 1.0f;   // pitch-bend multiplier, 1.0 = centred
    float lfoPitchRatio_ = 1.0f, lfoAmp_ = 1.0f, lfoVal_ = 0.0f, lfoPhase_ = 0.0f, lfoHeld_ = 0.0f;
    float modWheel_ = 0.0f, blockKeyTrack_ = 0.0f;
    uint32_t rng_ = 12345u;
    SVF::Mode filterMode_ = SVF::Mode::Lowpass;
    float pitchRatio() const { return bendRatio_ * lfoPitchRatio_; }
    Patch live_;                                  // the knobs, as a Patch (refreshed every block)
    std::vector<Patch> progPatches_;              // each program's stored values as a Patch (for Combi parts)
    const Patch& patchOf(const Voice& v) const { return v.frozen ? v.p : live_; }
    void applyPitch(Voice& v)
    {
        if (v.region) v.inc = v.baseInc * pitchRatio();
        else { v.osc1.setFrequency(v.baseHz * pitchRatio()); v.osc2.setFrequency(v.baseHz * patchOf(v).osc2Ratio * pitchRatio()); }
    }
    std::deque<std::string> userNames_;
    void addProg(const char* name, std::vector<float> values)
    {
        addProgram(name, values);
        const std::vector<float> vals = values;
        progPatches_.push_back(buildPatch([&vals](int i) { return i < (int)vals.size() ? vals[(size_t)i] : 0.0f; }));
    }

    Voice& findFreeVoice()
    {
        Voice* best = &voices_[0];
        for (Voice& v : voices_)
        {
            if (!v.isActive()) return v;
            if (v.startOrder < best->startOrder) best = &v;   // steal the oldest
        }
        return *best;
    }

    m3::SampleBank bank_;
    Voice    voices_[kNumVoices];
    float    sampleRate_ = 44100.0f;
    float    smoothedGain_ = 0.5f;
    uint32_t noteCounter_ = 0;
    int      ctrlCountdown_ = 0;
    float    blockCutoff_ = 3000.0f;
    float    blockReso_ = 0.2f;
    float    blockFenv_ = 0.4f;
};

// This macro creates the exported VSTPluginMain entry point. Keep it last.
MPC_PLUGIN_ENTRY(M3Workstation)

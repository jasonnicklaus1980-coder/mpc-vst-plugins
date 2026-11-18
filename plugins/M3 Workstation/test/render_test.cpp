// Host harness for the M3 Workstation sample engine. Loads the plugin .so, selects a program,
// plays MIDI and measures the rendered audio.
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <array>
#include "../../../framework/mpc_vst2_abi.h"

typedef AEffect* (*MainFn)(audioMasterCallback);
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) { return op == audioMasterVersion ? 2400 : 0; }

struct Plug
{
    AEffect* e = nullptr;
    const float sr = 44100.0f;
    explicit Plug(MainFn m)
    {
        e = m(host);
        e->dispatcher(e, effOpen, 0, 0, nullptr, 0);
        e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, sr);
        e->dispatcher(e, effSetBlockSize, 0, 128, nullptr, 0);
        e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);
        e->dispatcher(e, effStartProcess, 0, 0, nullptr, 0);
    }
    ~Plug() { e->dispatcher(e, effMainsChanged, 0, 0, nullptr, 0); e->dispatcher(e, effClose, 0, 0, nullptr, 0); }
    void program(int i) { e->dispatcher(e, effSetProgram, 0, i, nullptr, 0); }
    void param(int i, float v) { e->setParameter(e, i, v); }
    int bend = 8192;   // pitch-bend sent before the note (8192 = centre)
    int modwheel = 0;  // CC1 sent before the note
    std::vector<std::array<int, 3>> extra;   // more MIDI events sent with the first block of a render() call
    // Render `seconds`, optionally sending one MIDI event at frame 0 of the first block.
    std::vector<float> render(double seconds, int status = 0, int d1 = 0, int d2 = 0)
    {
        const int block = 128, total = (int)(seconds * sr);
        std::vector<float> L(total), R(total);
        std::vector<float> bl(block), br(block);
        float* outs[2] = {bl.data(), br.data()};
        bool first = true;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min(block, total - pos);
            std::fill(bl.begin(), bl.end(), 0.0f); std::fill(br.begin(), br.end(), 0.0f);
            if (first && (status || !extra.empty()))
            {
                std::vector<VstMidiEvent> evs;
                auto add = [&](int st, int a1, int a2) { VstMidiEvent ev; std::memset(&ev, 0, sizeof ev); ev.type = kVstMidiType;
                    ev.byteSize = sizeof ev; ev.midiData[0] = (char)st; ev.midiData[1] = (char)a1; ev.midiData[2] = (char)a2; evs.push_back(ev); };
                add(0xE0, bend & 127, bend >> 7); add(0xB0, 1, modwheel);
                for (auto& x : extra) add(x[0], x[1], x[2]);
                if (status) add(status, d1, d2);
                struct { VstInt32 n; VstIntPtr r; VstEvent* ev[32]; } pk; pk.n = (VstInt32)evs.size(); pk.r = 0;
                for (size_t k = 0; k < evs.size(); ++k) pk.ev[k] = (VstEvent*)&evs[k];
                e->dispatcher(e, effProcessEvents, 0, 0, &pk, 0);
            }
            first = false;
            e->processReplacing(e, nullptr, outs, n);
            for (int i = 0; i < n; ++i) { L[pos + i] = bl[i]; R[pos + i] = br[i]; }
        }
        // interleave as L only for analysis; keep R in the tail for pan checks
        L.insert(L.end(), R.begin(), R.end());
        return L;
    }
};

static double rms(const std::vector<float>& x, double t0, double t1, double sr)
{
    size_t a = (size_t)(t0 * sr), b = (size_t)(t1 * sr); double s = 0;
    for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / (double)(b - a));
}
static double freq(const std::vector<float>& x, double t0, double t1, double sr)
{
    size_t a = (size_t)(t0 * sr), b = (size_t)(t1 * sr); int c = 0;
    for (size_t i = a + 1; i < b; ++i) if (x[i - 1] <= 0 && x[i] > 0) ++c;
    return c / (t1 - t0);
}
static bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }

static int fails = 0;
#define CHECK(cond, ...) do { bool ok_ = (cond); std::printf("%s  ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); if (!ok_) ++fails; } while (0)

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: render_test plugin.so synth|vocal\n"); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW);
    if (!h) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    MainFn m = (MainFn)dlsym(h, "VSTPluginMain");
    if (!m) { std::fprintf(stderr, "no VSTPluginMain\n"); return 2; }
    const double sr = 44100.0;
    const int kLoopParam = 15, kInstParam = 14;
    const int kSamplePadProgram = 4;

    if (std::strcmp(argv[2], "presets") == 0)
    {   // Render every factory preset (programs 8+): finite, audible, not clipping. CAL_OUT=file writes "name<TAB>peak".
        Plug probe(m);
        const int n = probe.e->numPrograms;
        CHECK(n >= 108, "program count %d (8 built-in + factory bank)", n);
        FILE* cal = std::getenv("CAL_OUT") ? std::fopen(std::getenv("CAL_OUT"), "w") : nullptr;
        int bad = 0;
        for (int i = 8; i < n; ++i)
        {
            char name[64] = {0};
            probe.e->dispatcher(probe.e, effGetProgramNameIndexed, i, 0, name, 0);
            Plug p(m); p.program(i);
            const bool drums = std::strstr(name, "DRM ") == name || std::strstr(name, "PRC ") == name || std::strstr(name, "TRP Trap Kit") == name;
            std::vector<float> all;
            const int notes[3] = {drums ? 36 : 60, drums ? 38 : 64, drums ? 42 : 67};
            auto x = p.render(1.5, 0x90, notes[0], 100);
            auto x2 = p.render(0.5, 0x90, notes[1], 100);
            auto x3 = p.render(0.5, 0x90, notes[2], 100);
            double peak = 0, e2 = 0; size_t cnt = 0;
            bool fin = finite(x) && finite(x2) && finite(x3);
            for (auto* v : {&x, &x2, &x3}) for (float f : *v) { peak = std::max(peak, (double)std::fabs(f)); e2 += (double)f * f; ++cnt; }
            const double r = std::sqrt(e2 / (double)cnt);
            const bool ok = fin && peak < 1.0 && r > 1e-4;
            if (!ok) { ++bad; CHECK(false, "preset %d '%s': finite=%d peak=%.3f rms=%.5f", i, name, (int)fin, peak, r); }
            std::printf("  %3d %-24s peak %.3f rms %.4f\n", i, name, peak, r);
            if (cal) std::fprintf(cal, "%s\t%.5f\t%.5f\n", name, peak, r);
        }
        CHECK(bad == 0, "%d presets rendered, %d problems", n - 8, bad);
        if (cal) std::fclose(cal);
        std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
        return fails ? 1 : 0;
    }

    if (std::strcmp(argv[2], "content") == 0)
    {   // Every instrument in Content/Instruments: notes across the keyboard are finite, below full scale, and audible where mapped.
        const int count = argc > 3 ? std::atoi(argv[3]) : 8;
        for (int inst = 0; inst < count; ++inst)
        {
            int heard = 0, played = 0; double worst = 0;
            bool fin = true;
            for (int note = 24; note <= 108; note += 6)
            {
                Plug p(m); p.program(kSamplePadProgram); p.param(kInstParam, inst / 7.0f); p.param(kLoopParam, 1.0f);
                auto x = p.render(1.0, 0x90, note, 110);
                fin = fin && finite(x);
                double pk = 0; for (float f : x) pk = std::max(pk, (double)std::fabs(f));
                worst = std::max(worst, pk); ++played;
                if (rms(x, 0.05, 0.6, sr) > 1e-3) ++heard;
            }
            CHECK(fin && worst < 1.0 && heard > 0, "instrument %d: %d/%d test notes audible, peak %.3f", inst + 1, heard, played, worst);
        }
        std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
        return fails ? 1 : 0;
    }

    if (std::strcmp(argv[2], "userpresets") == 0)
    {   // Programs after the 124 built-in ones come from Content/Presets/user.m3p: each must load, be finite, audible, below full scale.
        Plug probe(m);
        const int n = probe.e->numPrograms, first = 124;
        CHECK(n > first, "user presets loaded: %d (programs %d total)", n - first, n);
        int bad = 0, quiet = 0;
        FILE* cal = std::getenv("CAL_OUT") ? std::fopen(std::getenv("CAL_OUT"), "w") : nullptr;
        for (int i = first; i < n; ++i)
        {
            char name[64] = {0};
            probe.e->dispatcher(probe.e, effGetProgramNameIndexed, i, 0, name, 0);
            Plug p(m); p.program(i);
            std::vector<float> x = p.render(1.2, 0x90, 60, 100), x2 = p.render(0.4, 0x90, 36, 100);
            double peak = 0, e2 = 0; size_t cnt = 0;
            const bool fin = finite(x) && finite(x2);
            for (auto* v : {&x, &x2}) for (float f : *v) { peak = std::max(peak, (double)std::fabs(f)); e2 += (double)f * f; ++cnt; }
            const double r = std::sqrt(e2 / (double)cnt);
            if (cal) std::fprintf(cal, "%d\t%.5f\t%.5f\n", i - first, peak, r);
            if (!fin || peak >= 1.0) { ++bad; std::printf("  BAD  %3d %-24s finite=%d peak=%.3f\n", i, name, (int)fin, peak); }
            if (r < 1e-4) { ++quiet; std::printf("  SILENT %3d %-22s rms=%.6f\n", i, name, r); }
        }
        CHECK(bad == 0, "no non-finite or clipping user presets (%d bad)", bad);
        CHECK(quiet == 0, "every user preset is audible (%d silent)", quiet);
        std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
        return fails ? 1 : 0;
    }

    if (std::strcmp(argv[2], "synth") == 0)
    {
        auto pitch = [&](int note, double expect, const char* what) {
            Plug p(m); p.program(kSamplePadProgram);
            auto x = p.render(2.0, 0x90, note, 127);
            double f = freq(x, 0.8, 1.8, sr);
            CHECK(finite(x) && std::fabs(f - expect) / expect < 0.01, "%s: note %d -> %.1f Hz (expected %.1f)", what, note, f, expect);
        };
        pitch(69, 440.0, "root key, 22.05 kHz sample at 44.1 kHz host");
        pitch(81, 880.0, "octave up");
        pitch(57, 220.0, "octave down");

        { // loop on keeps sounding past the 1 s sample; loop off ends
            Plug on(m); on.program(kSamplePadProgram); on.param(kLoopParam, 1.0f);
            auto a = on.render(4.0, 0x90, 69, 127);
            double early = rms(a, 1.0, 2.0, sr), late = rms(a, 3.0, 4.0, sr);
            CHECK(early > 0.01 && late > 0.5 * early, "loop on sustains: rms %.4f (1-2 s) vs %.4f (3-4 s)", early, late);
            Plug off(m); off.program(kSamplePadProgram); off.param(kLoopParam, 0.0f);
            auto b = off.render(4.0, 0x90, 69, 127);
            double l2 = rms(b, 3.0, 4.0, sr);
            CHECK(rms(b, 0.5, 0.9, sr) > 0.01 && l2 < 1e-4, "loop off ends after the sample: rms %.6f at 3-4 s", l2);
        }
        { // release decays to silence
            Plug p(m); p.program(kSamplePadProgram);
            p.render(1.0, 0x90, 69, 127);
            auto r = p.render(3.0, 0x80, 69, 0);
            CHECK(rms(r, 2.5, 3.0, sr) < 1e-4, "note off releases to silence: rms %.6f", rms(r, 2.5, 3.0, sr));
        }
        { // second instrument: zone 60..72, root 66, +100 cents
            Plug p(m); p.program(kSamplePadProgram); p.param(kInstParam, 1.0f / 7.0f);
            auto in = p.render(2.0, 0x90, 66, 127);
            double f = freq(in, 0.4, 0.9, sr), expect = 440.0 * std::pow(2.0, 100.0 / 1200.0);
            CHECK(std::fabs(f - expect) / expect < 0.01, "instrument 2, +100 cents tune: %.1f Hz (expected %.1f)", f, expect);
            Plug q(m); q.program(kSamplePadProgram); q.param(kInstParam, 1.0f / 7.0f);
            auto out = q.render(1.5, 0x90, 40, 127);
            CHECK(rms(out, 0.0, 1.5, sr) < 1e-6, "note outside the zone is silent (rms %.8f)", rms(out, 0.0, 1.5, sr));
        }
        { // pitch bend: full up = +2 semitones, full down = -2, in sample mode
            const double up = 440.0 * std::pow(2.0, 2.0 / 12.0), dn = 440.0 * std::pow(2.0, -2.0 / 12.0);
            Plug a(m); a.program(kSamplePadProgram); a.bend = 16383;
            double fu = freq(a.render(2.0, 0x90, 69, 127), 0.4, 0.9, sr);
            Plug b(m); b.program(kSamplePadProgram); b.bend = 0;
            double fd = freq(b.render(2.0, 0x90, 69, 127), 0.4, 0.9, sr);
            CHECK(std::fabs(fu - up) / up < 0.015 && std::fabs(fd - dn) / dn < 0.015, "pitch bend +/-2 st (sample): %.1f / %.1f Hz (expected %.1f / %.1f)", fu, fd, up, dn);
            Plug c(m); c.program(0); c.bend = 16383;
            double fs = freq(c.render(1.0, 0x90, 69, 127), 0.4, 0.9, sr);
            Plug d(m); d.program(0);
            double f0 = freq(d.render(1.0, 0x90, 69, 127), 0.4, 0.9, sr);
            CHECK(fs / f0 > 1.10 && fs / f0 < 1.13, "pitch bend (synth): ratio %.3f (expected 1.122)", fs / f0);
        }
        { // filter types, key tracking, LFO (params are normalised 0..1; indices 16..21)
            const int kFType = 16, kKTrack = 17, kLShape = 18, kLDepth = 20, kLDest = 21;
            auto level = [&](int ftype, int note) { Plug p(m); p.program(0); p.param(kFType, ftype / 3.0f);
                                                    return rms(p.render(1.0, 0x90, note, 127), 0.3, 1.0, sr); };
            double lp = level(0, 36), hp = level(1, 36);
            CHECK(lp > 0.01 && hp < 0.5 * lp, "filter type: lowpass rms %.4f vs highpass %.4f on a low saw", lp, hp);
            double bp = level(2, 36), nt = level(3, 36);
            CHECK(bp > 0.0 && bp < lp && nt > 0.5 * lp, "bandpass %.4f below lowpass; notch %.4f keeps the low note", bp, nt);
            auto kt = [&](float amt) { Plug p(m); p.program(0); p.param(6, 0.4f);   // cutoff ~300 Hz
                                       p.param(kKTrack, amt);
                                       return rms(p.render(1.0, 0x90, 84, 127), 0.3, 1.0, sr); };
            double k0 = kt(0.0f), k1 = kt(1.0f);
            CHECK(k1 > 1.5 * k0, "key tracking: a high note passes the filter with 100%% tracking (rms %.4f vs %.4f)", k1, k0);
            auto spread = [&](Plug& p) { auto x = p.render(1.5, 0x90, 69, 127); double lo = 1e9, hi = 0;
                                         for (double t = 0.2; t < 1.4; t += 0.05) { double f = freq(x, t, t + 0.05, sr); lo = std::min(lo, f); hi = std::max(hi, f); }
                                         return hi / std::max(lo, 1.0); };
            auto sine = [&](Plug& p) { p.param(1, 0.0f); p.param(2, 0.0f); p.param(5, 0.0f); };   // osc1 sine only
            Plug off(m); off.program(0); sine(off);
            Plug on(m); on.program(0); sine(on); on.param(kLDepth, 1.0f);
            double so = spread(off), sn = spread(on);
            CHECK(so < 1.05 && sn > 1.10, "LFO -> pitch: pitch spread %.3f with depth 0, %.3f with depth 100%%", so, sn);
            Plug lo(m); lo.program(0); sine(lo); lo.param(kLDepth, 1.0f); lo.param(22, 0.0f);
            CHECK(spread(lo) < 1.05, "LFO On switch off silences the LFO: pitch spread %.3f", spread(lo));
            Plug mw(m); mw.program(0); sine(mw); mw.modwheel = 127;
            double sm = spread(mw);
            CHECK(sm > 1.10, "mod wheel (CC1) drives LFO depth: pitch spread %.3f", sm);
            Plug am(m); am.program(0); am.param(kLDepth, 1.0f); am.param(kLDest, 1.0f); am.param(kLShape, 2.0f / 3.0f);
            auto x = am.render(2.0, 0x90, 60, 127);
            double mn = 1e9, mx = 0;
            for (double t = 0.4; t < 1.9; t += 0.05) { double r = rms(x, t, t + 0.05, sr); mn = std::min(mn, r); mx = std::max(mx, r); }
            CHECK(mx > 3.0 * std::max(mn, 1e-6), "LFO -> amp (tremolo, square): rms swings %.4f..%.4f", mn, mx);
        }
        { // master effects (params 23..28: chorus, delay mix/time/fb, reverb mix/size); Pluck dies out by ~0.5 s
            auto tail = [&](int prm, float v, double t0, double t1, int p2 = -1, float v2 = 0) {
                Plug p(m); p.program(2); p.param(prm, v); if (prm == 24) p.param(25, 0.4949f); if (p2 >= 0 && p2 != 25) p.param(p2, v2);
                auto x = p.render(2.0, 0x90, 60, 127); return std::make_pair(rms(x, t0, t1, sr), finite(x)); };
            auto dry = tail(24, 0.0f, 0.62, 0.9), ech = tail(24, 1.0f, 0.62, 0.9, 25, 0.4949f);
            CHECK(ech.second && ech.first > 20 * std::max(dry.first, 1e-6), "delay (500 ms): echo rms %.4f vs %.6f without delay", ech.first, dry.first);
            auto fbOff = tail(24, 1.0f, 1.2, 1.5, 26, 0.0f), fbOn = tail(24, 1.0f, 1.2, 1.5, 26, 1.0f);
            CHECK(fbOn.first > 5 * std::max(fbOff.first, 1e-6), "delay feedback adds repeats: rms %.4f (fb 90%%) vs %.6f (fb 0)", fbOn.first, fbOff.first);
            auto rd = tail(27, 0.0f, 1.0, 1.5), rw = tail(27, 1.0f, 1.0, 1.5, 28, 1.0f);
            CHECK(rw.second && rw.first > 20 * std::max(rd.first, 1e-6), "reverb tail: rms %.4f vs %.6f dry (1.0-1.5 s)", rw.first, rd.first);
            Plug c0(m); c0.program(1); Plug c1(m); c1.program(1); c1.param(23, 1.0f);
            auto a = c0.render(1.5, 0x90, 60, 127), b = c1.render(1.5, 0x90, 60, 127);
            double r0 = rms(a, 0.5, 1.4, sr), r1 = rms(b, 0.5, 1.4, sr), diff = 0;
            for (size_t i = (size_t)(0.5 * sr); i < (size_t)(1.4 * sr); ++i) diff += std::fabs(a[i] - b[i]);
            CHECK(finite(b) && diff > 1.0 && r1 > 0.5 * r0 && r1 < 2.0 * r0, "chorus changes the sound without blowing up: rms %.4f -> %.4f", r0, r1);
        }
        { // arpeggiator (params 29..35: on, mode, rate, gate, octaves, swing, latch). 120 bpm default, 1/8 = 0.25 s per step.
            const double C = 261.63, E = 329.63, G = 392.0;
            auto setup = [&](Plug& p, int mode, int rate, float gate = 0.7778f, int oct = 1, bool latch = false) {
                p.program(0); p.param(1, 0.0f); p.param(2, 0.0f); p.param(5, 0.0f);   // osc1 sine only
                p.param(9, 0.0f); p.param(12, 0.0f); p.param(11, 1.0f);              // 1 ms attack/release, full sustain
                p.param(29, 1.0f); p.param(30, mode / 5.0f); p.param(31, rate / 5.0f); p.param(32, gate);
                p.param(33, (oct - 1) / 3.0f); p.param(35, latch ? 1.0f : 0.0f);
                p.extra = {{0x90, 60, 100}, {0x90, 64, 100}}; };
            auto steps = [&](const std::vector<float>& x, int n) { std::vector<double> f; for (int k = 0; k < n; ++k)
                f.push_back(freq(x, 0.25 * k + 0.06, 0.25 * k + 0.16, sr)); return f; };
            auto close = [&](const std::vector<double>& f, const std::vector<double>& e) { for (size_t i = 0; i < e.size(); ++i)
                if (std::fabs(f[i] - e[i]) / e[i] > 0.04) return false; return true; };
            auto show = [&](const std::vector<double>& f) { std::string r; char b[16]; for (double v : f) { std::snprintf(b, sizeof b, "%.0f ", v); r += b; } return r; };
            { Plug p(m); setup(p, 0, 1); p.extra.push_back({0x90, 67, 100}); auto f = steps(p.render(2.0), 6);
              CHECK(close(f, {C, E, G, C, E, G}), "arp Up, 1/8: %s Hz", show(f).c_str()); }
            { Plug p(m); setup(p, 1, 1); p.extra.push_back({0x90, 67, 100}); auto f = steps(p.render(2.0), 6);
              CHECK(close(f, {G, E, C, G, E, C}), "arp Down: %s Hz", show(f).c_str()); }
            { Plug p(m); setup(p, 2, 1); p.extra.push_back({0x90, 67, 100}); auto f = steps(p.render(2.5), 8);
              CHECK(close(f, {C, E, G, E, C, E, G, E}), "arp Up/Down: %s Hz", show(f).c_str()); }
            { Plug p(m); setup(p, 0, 1, 0.7778f, 2); auto f = steps(p.render(2.0), 4);
              CHECK(close(f, {C, E, C * 2, E * 2}), "arp Up over 2 octaves: %s Hz", show(f).c_str()); }
            { Plug p(m); setup(p, 4, 1); p.extra = {{0x90, 67, 100}, {0x90, 60, 100}}; auto f = steps(p.render(2.0), 4);
              CHECK(close(f, {G, C, G, C}), "arp Order (played G then C): %s Hz", show(f).c_str()); }
            { Plug p(m); setup(p, 3, 1); p.extra.push_back({0x90, 67, 100}); auto f = steps(p.render(3.0), 12); bool ok = true;
              for (double v : f) ok = ok && (std::fabs(v - C) / C < 0.04 || std::fabs(v - E) / E < 0.04 || std::fabs(v - G) / G < 0.04);
              CHECK(ok, "arp Random stays on held notes: %s Hz", show(f).c_str()); }
            { Plug p(m); setup(p, 0, 1, 0.1667f); auto x = p.render(1.0);   // 25 % gate: sounding early in a step, silent late
              CHECK(rms(x, 0.30, 0.38, sr) > 0.01 && rms(x, 0.44, 0.49, sr) < 1e-4, "arp gate 25%%: rms %.4f early vs %.6f late in the step", rms(x, 0.30, 0.38, sr), rms(x, 0.44, 0.49, sr)); }
            { Plug p(m); setup(p, 0, 1); p.render(1.0); p.extra = {{0x80, 60, 0}, {0x80, 64, 0}};
              auto x = p.render(1.0); CHECK(rms(x, 0.3, 1.0, sr) < 1e-4, "arp stops when keys are released: rms %.6f", rms(x, 0.3, 1.0, sr)); }
            { Plug p(m); setup(p, 0, 1, 0.7778f, 1, true); p.render(1.0); p.extra = {{0x80, 60, 0}, {0x80, 64, 0}};
              auto x = p.render(1.0); CHECK(rms(x, 0.3, 1.0, sr) > 0.01, "arp latch keeps playing after release: rms %.4f", rms(x, 0.3, 1.0, sr)); }
            { Plug p(m); setup(p, 5, 1); p.param(30, 1.0f); p.extra = {{0x90, 60, 100}, {0x90, 64, 100}, {0x90, 67, 100}}; auto x = p.render(1.0);
              CHECK(finite(x) && rms(x, 0.06, 0.2, sr) > 0.01, "arp chord mode sounds: rms %.4f", rms(x, 0.06, 0.2, sr)); }
            { Plug p(m); p.program(6); auto x = p.render(1.5, 0x90, 60, 110);
              CHECK(finite(x) && rms(x, 0.2, 1.4, sr) > 0.005, "Arp Lead program plays: rms %.4f", rms(x, 0.2, 1.4, sr)); }
        }
        { // Drum mode (program 7 "Drum Kit", built-in synth kit). Params: 36 tune, 37 decay, 38 kit.
            auto hit = [&](int note, int vel, double secs, float tune = 0.5f, float decay = 1.0f / 3.0f) {
                Plug p(m); p.program(7); p.param(36, tune); p.param(37, decay);
                return p.render(secs, 0x90, note, vel); };
            auto kick = hit(36, 127, 1.0);
            double fk = freq(kick, 0.12, 0.40, sr);
            CHECK(finite(kick) && rms(kick, 0.0, 0.2, sr) > 0.02 && fk > 45 && fk < 80, "kick: rms %.3f, %.0f Hz body after the pitch drop", rms(kick, 0.0, 0.2, sr), fk);
            auto sn = hit(38, 127, 0.6);
            CHECK(rms(sn, 0.0, 0.15, sr) > 0.02 && freq(sn, 0.0, 0.1, sr) > 800.0, "snare: noisy and bright (%.0f Hz crossings, rms %.3f)", freq(sn, 0.0, 0.1, sr), rms(sn, 0.0, 0.15, sr));
            auto soft = hit(38, 30, 0.6);
            CHECK(rms(soft, 0.0, 0.15, sr) < 0.5 * rms(sn, 0.0, 0.15, sr), "velocity: soft hit rms %.3f vs hard %.3f", rms(soft, 0.0, 0.15, sr), rms(sn, 0.0, 0.15, sr));
            { Plug a(m); a.program(7); auto ho = a.render(0.8, 0x90, 46, 110);
              Plug b(m); b.program(7); b.extra = {{0x90, 46, 110}}; auto hc = b.render(0.8, 0x90, 42, 110);
              CHECK(rms(ho, 0.12, 0.3, sr) > 0.001 && rms(hc, 0.12, 0.3, sr) < 0.2 * rms(ho, 0.12, 0.3, sr), "hat choke: open hat rings (rms %.4f) but closed hat cuts it (rms %.5f)", rms(ho, 0.12, 0.3, sr), rms(hc, 0.12, 0.3, sr)); }
            double t0 = freq(hit(45, 110, 0.5), 0.05, 0.25, sr), t1 = freq(hit(45, 110, 0.5, 1.0f), 0.05, 0.25, sr);
            CHECK(t1 / t0 > 1.85 && t1 / t0 < 2.15, "drum tune +12 st doubles tom pitch: %.0f -> %.0f Hz", t0, t1);
            double d1 = rms(hit(49, 110, 1.5), 0.9, 1.3, sr), d2 = rms(hit(49, 110, 1.5, 0.5f, 1.0f), 0.9, 1.3, sr);
            CHECK(d2 > 3.0 * std::max(d1, 1e-6), "drum decay x2 lengthens a crash: tail rms %.4f vs %.5f", d2, d1);
            CHECK(rms(hit(20, 110, 0.5), 0.0, 0.5, sr) < 1e-6, "note outside the kit map is silent");
            { Plug p(m); p.program(7); p.extra = {{0x90, 36, 110}, {0x90, 38, 110}, {0x90, 42, 110}, {0x90, 49, 110}, {0x90, 45, 110},
                  {0x90, 47, 110}, {0x90, 50, 110}, {0x90, 56, 110}, {0x90, 39, 110}, {0x90, 51, 110}};
              auto x = p.render(1.0, 0x90, 40, 110); float pk = 0; for (float v : x) pk = std::max(pk, std::fabs(v));
              CHECK(finite(x) && pk < 1.0f && pk > 0.05f, "11 drums at once (more than the 8 drum voices): finite, peak %.2f", pk); }
            { Plug p(m); p.program(7); p.param(38, 1.0f); auto x = p.render(1.0, 0x90, 60, 110);   // sample kit: instrument 1 one-shot
              CHECK(finite(x), "sample-kit mode runs (instrument 1 mapped as one-shots): rms %.4f", rms(x, 0.0, 1.0, sr)); }
        }
        { // Combination mode (params 39 Mode, 40 Combination). 0 Pad+Pluck, 1 Bass/Lead, 2 Velocity Switch, 3 Layer Stack, 4 Drums/Lead
            auto combi = [&](int idx, int note, int vel, double secs, std::vector<std::array<int, 3>> more = {}) {
                Plug p(m); p.program(0); p.param(39, 1.0f); p.param(40, idx / 6.0f); p.extra = more; return p.render(secs, 0x90, note, vel); };
            auto lo = combi(2, 60, 50, 2.0), hi = combi(2, 60, 120, 2.0);
            CHECK(rms(lo, 0.0, 0.2, sr) > 0.002 && rms(lo, 1.0, 1.5, sr) < 1e-3 && rms(hi, 1.0, 1.5, sr) > 0.01,
                  "velocity switch: soft = Pluck (rms %.5f at 1-1.5 s), hard = Lead (rms %.4f)", rms(lo, 1.0, 1.5, sr), rms(hi, 1.0, 1.5, sr));
            auto dr = combi(4, 36, 110, 2.0), ld = combi(4, 72, 110, 2.0);
            CHECK(rms(dr, 0.0, 0.2, sr) > 0.01 && rms(dr, 1.0, 1.5, sr) < 1e-4 && rms(ld, 1.0, 1.5, sr) > 0.01,
                  "keyboard split: low key = drum kick (tail rms %.6f), high key = Lead (rms %.4f)", rms(dr, 1.0, 1.5, sr), rms(ld, 1.0, 1.5, sr));
            Plug pad(m); pad.program(1); pad.param(23, 0.0f); pad.param(27, 0.0f); pad.param(24, 0.0f); auto padOnly = pad.render(0.5, 0x90, 60, 100);   // effects off
            auto lay = combi(0, 60, 100, 0.5);
            CHECK(rms(lay, 0.02, 0.1, sr) > 1.15 * rms(padOnly, 0.02, 0.1, sr) && rms(lay, 0.02, 0.1, sr) > 0.001, "layering: Pad+Pluck early rms %.4f vs Pad alone %.4f (the Pluck layer is audible)", rms(lay, 0.02, 0.1, sr), rms(padOnly, 0.02, 0.1, sr));
            auto top = combi(3, 120, 100, 1.0);
            CHECK(finite(top) && rms(top, 0.1, 0.5, sr) > 0.001, "transposed part past key 127 is dropped, the rest still plays: rms %.4f", rms(top, 0.1, 0.5, sr));
            auto many = combi(3, 48, 100, 1.5, {{0x90, 52, 100}, {0x90, 55, 100}, {0x90, 60, 100}, {0x90, 64, 100}, {0x90, 67, 100}});
            float pk = 0; for (float v : many) pk = std::max(pk, std::fabs(v));
            CHECK(finite(many) && pk < 1.2f && pk > 0.01f, "6 notes x 3 parts (voice stealing): finite, peak %.2f", pk);
            { Plug p(m); p.program(0); p.param(39, 1.0f); p.param(40, 3.0f / 6.0f); p.render(1.0, 0x90, 60, 100);
              p.extra = {}; auto x = p.render(3.0, 0x80, 60, 0);
              CHECK(rms(x, 2.5, 3.0, sr) < 1e-4, "combi note-off releases every part: rms %.6f", rms(x, 2.5, 3.0, sr)); }
        }
        { // Source = Synth still works
            Plug p(m); p.program(0);
            auto x = p.render(1.0, 0x90, 60, 100);
            CHECK(rms(x, 0.3, 1.0, sr) > 0.005, "synth source unaffected: rms %.4f", rms(x, 0.3, 1.0, sr));
        }
    }
    else
    {
        for (int note : {52, 60, 72, 90, 100})
        {
            Plug p(m); p.program(kSamplePadProgram);
            auto x = p.render(3.0, 0x90, note, 110);
            double r = rms(x, 1.0, 3.0, sr);
            if (note <= 96) CHECK(finite(x) && r > 0.005, "vocal pad note %d: rms %.4f over 1-3 s (sustained through loop)", note, r);
            else CHECK(r < 1e-6, "vocal pad note %d is above the last zone: silent (rms %.8f)", note, r);
        }
        { // peak level sanity
            Plug p(m); p.program(kSamplePadProgram);
            auto x = p.render(2.0, 0x90, 60, 127);
            float pk = 0; for (float v : x) pk = std::fmax(pk, std::fabs(v));
            CHECK(pk > 0.02f && pk < 1.0f, "vocal pad peak %.3f (no clipping)", pk);
        }
    }
    std::printf("%s (%d failures)\n", fails ? "RESULT: FAIL" : "RESULT: PASS", fails);
    return fails ? 1 : 0;
}

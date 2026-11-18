// Master effects for M3 Workstation: chorus -> stereo delay -> reverb.
// REALTIME: init() allocates (call from prepare() only); process() and clear() never allocate.
//
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace m3 {

// Circular delay line with linear-interpolated fractional reads.
struct DelayLine
{
    std::vector<float> buf; int pos = 0;
    void init(int size) { buf.assign((size_t)std::max(size, 4), 0.0f); pos = 0; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); pos = 0; }
    void push(float x) { buf[(size_t)pos] = x; if (++pos >= (int)buf.size()) pos = 0; }
    float read(float delay) const                       // delay in samples (>= 1)
    {
        const int n = (int)buf.size();
        delay = std::min(std::max(delay, 1.0f), (float)(n - 2));
        float rp = (float)pos - delay; if (rp < 0) rp += (float)n;
        const int i0 = (int)rp, i1 = (i0 + 1) % n; const float f = rp - (float)i0;
        return buf[(size_t)i0] * (1.0f - f) + buf[(size_t)i1] * f;
    }
};

// Two-voice stereo chorus (short modulated delay, mixed with the dry signal).
struct Chorus
{
    DelayLine dl[2]; float phase = 0.0f, sr = 44100.0f;
    void init(float sampleRate) { sr = sampleRate; for (auto& d : dl) d.init((int)(0.05f * sr)); phase = 0; }
    void clear() { for (auto& d : dl) d.clear(); phase = 0; }
    void process(float& l, float& r, float mix)
    {
        if (mix <= 0.0f) { dl[0].push(l); dl[1].push(r); return; }
        phase += 0.8f / sr; if (phase >= 1.0f) phase -= 1.0f;
        const float base = 0.0085f * sr, depth = 0.0035f * sr;
        const float wl = dl[0].read(base + depth * std::sin(6.2831853f * phase));
        const float wr = dl[1].read(base + depth * std::sin(6.2831853f * (phase + 0.5f)));
        dl[0].push(l); dl[1].push(r);
        l += mix * wl * 0.7f; r += mix * wr * 0.7f;   // wet added to the dry signal
    }
};

// Stereo delay, feedback runs through a one-pole low-pass so repeats darken.
struct StereoDelay
{
    DelayLine dl[2]; float sr = 44100.0f, cur = 0.0f, lp[2] = {0, 0};
    void init(float sampleRate) { sr = sampleRate; for (auto& d : dl) d.init((int)(1.2f * sr)); cur = 0.35f * sr; }
    void clear() { for (auto& d : dl) d.clear(); lp[0] = lp[1] = 0; }
    void process(float& l, float& r, float timeSec, float fb, float mix)
    {
        cur += 0.0005f * (timeSec * sr - cur);                // smooth time changes (no zipper)
        const float wl = dl[0].read(cur), wr = dl[1].read(cur);
        lp[0] += 0.45f * (wl - lp[0]); lp[1] += 0.45f * (wr - lp[1]);
        dl[0].push(l + lp[1] * fb); dl[1].push(r + lp[0] * fb);   // cross-fed: ping-pong
        l += mix * wl; r += mix * wr;
    }
};

// Freeverb-style reverb (8 damped combs + 4 allpasses per channel).
struct Reverb
{
    static constexpr int kC = 8, kA = 4;
    struct Comb { std::vector<float> b; int p = 0; float lp = 0;
        float run(float x, float fb, float damp) { float y = b[(size_t)p]; lp = y * (1 - damp) + lp * damp; b[(size_t)p] = x + lp * fb; if (++p >= (int)b.size()) p = 0; return y; } };
    struct AP { std::vector<float> b; int p = 0;
        float run(float x) { float y = b[(size_t)p]; b[(size_t)p] = x + y * 0.5f; if (++p >= (int)b.size()) p = 0; return y - x; } };
    Comb c[2][kC]; AP a[2][kA];
    void init(float sr)
    {
        static const int ct[kC] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, at[kA] = {556, 441, 341, 225};
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < kC; ++i) { c[ch][i].b.assign((size_t)((ct[i] + ch * 23) * sr / 44100.0f) + 1, 0.0f); c[ch][i].p = 0; c[ch][i].lp = 0; }
            for (int i = 0; i < kA; ++i) { a[ch][i].b.assign((size_t)((at[i] + ch * 23) * sr / 44100.0f) + 1, 0.0f); a[ch][i].p = 0; }
        }
    }
    void clear() { for (int ch = 0; ch < 2; ++ch) { for (auto& x : c[ch]) { std::fill(x.b.begin(), x.b.end(), 0.0f); x.lp = 0; } for (auto& x : a[ch]) std::fill(x.b.begin(), x.b.end(), 0.0f); } }
    void process(float& l, float& r, float size, float mix)
    {
        if (c[0][0].b.empty()) return;
        const float fb = 0.70f + 0.28f * size, in = (l + r) * 0.015f;
        float wl = 0, wr = 0;
        for (int i = 0; i < kC; ++i) { wl += c[0][i].run(in, fb, 0.3f); wr += c[1][i].run(in, fb, 0.3f); }
        for (int i = 0; i < kA; ++i) { wl = a[0][i].run(wl); wr = a[1][i].run(wr); }
        l += mix * wl * 3.0f; r += mix * wr * 3.0f;
    }
};

struct MasterFx
{
    Chorus chorus; StereoDelay delay; Reverb reverb;
    void init(float sr) { chorus.init(sr); delay.init(sr); reverb.init(sr); }
    void clear() { chorus.clear(); delay.clear(); reverb.clear(); }
};

} // namespace m3

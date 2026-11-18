// Built-in synthesized drum kit for M3 Workstation Drum mode (original synthesis, no samples).
// General-MIDI-style note map 35..59: kicks, snares, clap, rim, hats (one choke group), toms, cymbals, cowbell.
// REALTIME: fixed storage, no allocation.
//
// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>

namespace m3 {

struct DrumPad { int type; float freq; float decay; float pan; float level; int group; };   // type: see DrumVoice::process

inline const DrumPad* drumPadFor(int note)
{
    enum { Kick, Snare, Clap, Hat, Tom, Cym, Rim, Bell };
    static const DrumPad pads[] = {
        {Kick, 50, 0.45f, 0.0f, 1.0f, 0},   // 35 acoustic kick
        {Kick, 56, 0.38f, 0.0f, 1.0f, 0},   // 36 kick
        {Rim, 450, 0.06f, -0.2f, 0.8f, 0},  // 37 side stick
        {Snare, 190, 0.24f, 0.0f, 0.9f, 0}, // 38 snare
        {Clap, 0, 0.28f, 0.1f, 0.9f, 0},    // 39 clap
        {Snare, 225, 0.19f, 0.0f, 0.9f, 0}, // 40 electric snare
        {Tom, 85, 0.45f, -0.5f, 1.0f, 0},   // 41 low floor tom
        {Hat, 0, 0.05f, 0.3f, 0.7f, 1},     // 42 closed hat
        {Tom, 105, 0.42f, -0.35f, 1.0f, 0}, // 43 high floor tom
        {Hat, 0, 0.09f, 0.3f, 0.7f, 1},     // 44 pedal hat
        {Tom, 125, 0.38f, -0.15f, 1.0f, 0}, // 45 low tom
        {Hat, 0, 0.45f, 0.3f, 0.7f, 1},     // 46 open hat
        {Tom, 150, 0.36f, 0.15f, 1.0f, 0},  // 47 low-mid tom
        {Tom, 180, 0.34f, 0.3f, 1.0f, 0},   // 48 hi-mid tom
        {Cym, 0, 1.40f, -0.3f, 0.6f, 0},    // 49 crash
        {Tom, 215, 0.32f, 0.5f, 1.0f, 0},   // 50 high tom
        {Cym, 0, 0.90f, 0.4f, 0.55f, 0},    // 51 ride
        {Cym, 0, 1.00f, 0.1f, 0.5f, 0},     // 52 china
        {Cym, 0, 0.30f, -0.2f, 0.5f, 0},    // 53 ride bell (short)
        {Hat, 0, 0.12f, 0.35f, 0.6f, 0},    // 54 tambourine
        {Cym, 0, 0.80f, 0.35f, 0.55f, 0},   // 55 splash
        {Bell, 540, 0.30f, 0.25f, 0.7f, 0}, // 56 cowbell
        {Cym, 0, 1.60f, 0.3f, 0.6f, 0},     // 57 crash 2
        {Cym, 0, 0.50f, -0.1f, 0.5f, 0},    // 58 vibraslap-ish
        {Cym, 0, 0.70f, 0.45f, 0.55f, 0},   // 59 ride 2
    };
    return (note >= 35 && note <= 59) ? &pads[note - 35] : nullptr;
}

struct DrumVoice
{
    bool active = false; int note = 0, group = 0; DrumPad pad{}; float t = 0, vel = 1, ph1 = 0, ph2 = 0, ph3 = 0, lp = 0, fade = 1;
    bool choking = false; uint32_t rng = 22222u, age = 0;
    float panL = 1, panR = 1, level = 1, tune = 1, decay = 1;

    void start(int n, const DrumPad& p, int velocity, float tuneRatio, float decayScale, uint32_t order, float levelScale = 1.0f, float panOffset = 0.0f)
    {
        active = true; note = n; pad = p; group = p.group; t = 0; vel = velocity / 127.0f; ph1 = ph2 = ph3 = lp = 0; fade = 1;
        choking = false; age = order; tune = tuneRatio; decay = decayScale; level = p.level * levelScale;
        const float pp = p.pan + panOffset < -1.0f ? -1.0f : (p.pan + panOffset > 1.0f ? 1.0f : p.pan + panOffset);
        const float a = (pp + 1.0f) * 0.25f * 3.14159265f;
        panL = std::cos(a) * 1.41421356f; panR = std::sin(a) * 1.41421356f;
    }
    float noise() { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 8388608.0f - 1.0f; }
    static float sq(float ph) { return ph - std::floor(ph) < 0.5f ? 1.0f : -1.0f; }

    // Adds one sample to l/r.
    void process(float sr, float& l, float& r)
    {
        if (!active) return;
        const float dt = 1.0f / sr, len = pad.decay * decay;
        const float amp = std::exp(-6.9f * t / len);
        const float n = noise(); lp += 0.6f * (n - lp); const float hp = n - lp;       // crude high-passed noise
        float s = 0;
        switch (pad.type)
        {
            case 0: { const float f = pad.freq * tune * (1.0f + 3.0f * std::exp(-t * 40.0f)); ph1 += f * dt;       // kick
                      s = std::sin(6.2831853f * ph1) * amp + n * std::exp(-t * 700.0f) * 0.3f * vel; break; }
            case 1: { ph1 += pad.freq * tune * dt; ph2 += pad.freq * tune * 1.6f * dt;                              // snare
                      s = 0.5f * (std::sin(6.2831853f * ph1) + 0.4f * std::sin(6.2831853f * ph2)) * std::exp(-t * 20.0f)
                        + (0.5f + 0.4f * vel) * hp * amp; break; }
            case 2: { float e = std::exp(-t * 7.0f / len * 0.6f) * 0.4f;                                            // clap: 3 bursts + tail
                      for (int k = 0; k < 3; ++k) { const float tk = t - 0.011f * (float)k; if (tk >= 0) e += std::exp(-tk * 260.0f); }
                      s = hp * e * 0.7f; break; }
            case 3: { ph1 += 3100.0f * dt; ph2 += 4700.0f * dt;                                                     // hats / tambourine
                      s = (0.7f * hp + 0.3f * sq(ph1) * sq(ph2)) * amp; break; }
            case 4: { const float f = pad.freq * tune * (1.0f + 0.5f * std::exp(-t * 20.0f)); ph1 += f * dt;       // toms
                      s = std::sin(6.2831853f * ph1) * amp + 0.1f * hp * std::exp(-t * 300.0f); break; }
            case 5: { ph1 += 5100.0f * dt; ph2 += 6300.0f * dt; ph3 += 8200.0f * dt;                                // cymbals
                      s = (0.4f * (sq(ph1) + sq(ph2) + sq(ph3)) / 3.0f + 0.6f * hp) * amp; break; }
            case 6: { ph1 += pad.freq * tune * dt; s = std::sin(6.2831853f * ph1) * std::exp(-t * 180.0f) + 0.3f * n * std::exp(-t * 900.0f); break; }
            default: { ph1 += pad.freq * tune * dt; ph2 += pad.freq * tune * 1.48f * dt;                            // cowbell
                       s = 0.5f * (sq(ph1) + sq(ph2)) * amp * (t < 0.002f ? t / 0.002f : 1.0f); break; }
        }
        if (choking) { fade *= 0.992f; if (fade < 0.001f) active = false; }
        s *= (0.35f + 0.65f * vel * vel) * level * fade * 0.8f;
        l += s * panL; r += s * panR;
        t += dt;
        if (amp < 0.001f && t > 0.01f) active = false;
    }
};

} // namespace m3

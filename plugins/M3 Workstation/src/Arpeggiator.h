// Arpeggiator for M3 Workstation. Pure MIDI-in / MIDI-out, no dependency on the voice engine.
//
// Held notes become a sequence (Up, Down, Up/Down, Random, Order played, Chord) spread over 1-4 octaves,
// stepped at a tempo-synced rate with gate length and swing. Latch keeps the notes after the keys are released.
// Timing: input notes take effect at the start of the next block, so a new arp can start up to one block
// (<= 512 samples) late. REALTIME: no allocation; all storage is fixed-size.
//
// SPDX-License-Identifier: MIT
#pragma once
#include "mpc_plugin.h"
#include <algorithm>
#include <cstdint>

namespace m3 {

struct ArpSettings
{
    int   mode = 0;        // 0 Up, 1 Down, 2 Up/Down, 3 Random, 4 Order, 5 Chord
    int   rate = 3;        // 0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32
    int   octaves = 1;     // 1..4
    float gate = 0.8f;     // 0.1..1.0 of the step
    float swing = 0.0f;    // 0..0.5: odd steps are delayed
    bool  latch = false;
};

class Arpeggiator
{
public:
    static constexpr int kMaxHeld = 16, kMaxSeq = kMaxHeld * 4, kMaxOut = 64;

    void reset() { nHeld_ = nSound_ = physDown_ = 0; step_ = 0; stepCnt_ = 0; gateCnt_ = -1; order_ = 0; }

    void noteOn(int note, int vel)
    {
        if (physDown_ == 0 && latch_) nHeld_ = 0;          // a fresh chord replaces a latched one
        ++physDown_;
        for (int i = 0; i < nHeld_; ++i) if (held_[i].note == note) { held_[i].vel = vel; return; }
        if (nHeld_ < kMaxHeld) held_[nHeld_++] = {note, vel, order_++};
    }
    void noteOff(int note)
    {
        if (physDown_ > 0) --physDown_;
        if (latch_) return;
        for (int i = 0; i < nHeld_; ++i) if (held_[i].note == note) { held_[i] = held_[--nHeld_]; break; }
    }
    bool active() const { return nHeld_ > 0 || nSound_ > 0; }
    void clearHeld() { nHeld_ = 0; physDown_ = 0; }

    // Emit events for one block. `out` receives note-ons/offs sorted by sampleOffset; returns the count.
    int process(int numFrames, double bpm, double sampleRate, const ArpSettings& s, mpc::MidiEvent* out)
    {
        latch_ = s.latch;
        int n = 0;
        if (nHeld_ == 0)                                    // nothing to play: release anything still sounding
        {
            n = releaseAll(out, n, 0); stepCnt_ = 0; step_ = 0; return n;
        }
        static const double kBeats[6] = {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
        const double base = kBeats[std::min(std::max(s.rate, 0), 5)] * 60.0 / std::max(bpm, 20.0) * sampleRate;
        const float swing = std::min(std::max(s.swing, 0.0f), 0.5f);
        for (int i = 0; i < numFrames; ++i)
        {
            if (gateCnt_ >= 0 && --gateCnt_ < 0) n = releaseAll(out, n, i);
            if (--stepCnt_ > 0) continue;
            const double len = base * ((step_ & 1) ? 1.0 - swing : 1.0 + swing);
            stepCnt_ += (int)std::max(len, 1.0);
            n = releaseAll(out, n, i);                       // legato / short gate: end the previous step first
            n = trigger(out, n, i, s);
            gateCnt_ = (int)(len * std::min(std::max(s.gate, 0.1f), 1.0f));
            if (gateCnt_ >= (int)len) gateCnt_ = -1;         // 100 %: held until the next step releases it
            ++step_;
        }
        return n;
    }

private:
    struct Held { int note, vel, order; };
    Held held_[kMaxHeld]; int nHeld_ = 0, physDown_ = 0, order_ = 0;
    int sounding_[kMaxSeq]; int nSound_ = 0;
    int step_ = 0, stepCnt_ = 0, gateCnt_ = -1; bool latch_ = false; uint32_t rng_ = 2463534242u;

    int releaseAll(mpc::MidiEvent* out, int n, int off)
    {
        for (int i = 0; i < nSound_ && n < kMaxOut; ++i) out[n++] = mpc::MidiEvent::noteOff(0, sounding_[i], off);
        nSound_ = 0; gateCnt_ = -1; return n;
    }
    void emit(mpc::MidiEvent* out, int& n, int off, int note, int vel)
    {
        if (note < 0 || note > 127 || n >= kMaxOut || nSound_ >= kMaxSeq) return;
        out[n++] = mpc::MidiEvent::noteOn(0, note, vel, off); sounding_[nSound_++] = note;
    }
    int trigger(mpc::MidiEvent* out, int n, int off, const ArpSettings& s)
    {
        Held sorted[kMaxHeld]; std::copy(held_, held_ + nHeld_, sorted);
        if (s.mode == 4) std::sort(sorted, sorted + nHeld_, [](const Held& a, const Held& b) { return a.order < b.order; });
        else             std::sort(sorted, sorted + nHeld_, [](const Held& a, const Held& b) { return a.note < b.note; });
        const int oct = std::min(std::max(s.octaves, 1), 4);
        if (s.mode == 5)                                     // chord: every held note together, octave cycles per step
        {
            const int o = step_ % oct;
            for (int i = 0; i < nHeld_; ++i) emit(out, n, off, sorted[i].note + 12 * o, sorted[i].vel);
            return n;
        }
        Held seq[kMaxSeq]; int len = 0;
        for (int o = 0; o < oct; ++o)
            for (int i = 0; i < nHeld_ && len < kMaxSeq; ++i) seq[len++] = {sorted[i].note + 12 * o, sorted[i].vel, 0};
        int idx = 0;
        switch (s.mode)
        {
            case 1: idx = len - 1 - (step_ % len); break;                                      // Down
            case 2: { const int period = len > 1 ? 2 * len - 2 : 1; const int k = step_ % period;
                      idx = k < len ? k : period - k; break; }                                 // Up/Down
            case 3: rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; idx = (int)(rng_ % (uint32_t)len); break;
            default: idx = step_ % len; break;                                                 // Up, Order
        }
        emit(out, n, off, seq[idx].note, seq[idx].vel);
        return n;
    }
};

} // namespace m3

// SampleBank.h - multisample instruments loaded from the plugin's Content/ folder.
//
// Layout (written by tools/korg_samples.py `export`, or by hand):
//   Content/Instruments/index.txt        one .map file name per line
//   Content/Instruments/<name>.map       text keymap (see below)
//   Content/Samples/<set>/<file>.wav     mono or stereo WAV (stereo is mixed to mono)
//
// .map format, one instrument per file:
//   #M3MAP 1
//   name <display name>
//   region <wav under Content/> <low> <top> <root> <tuneCents> <level> <pan> <loopStart> <loopEnd>
// loopEnd <= loopStart means "no loop". level is read but not applied yet (unit unverified).
//
// REALTIME: load() allocates and reads files. Call it from the constructor only. After that the
// bank is read-only, so process() may read it without locks.
//
// SPDX-License-Identifier: MIT
#pragma once

#include "mpc_plugin.h"
#include "dsp/wav.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace m3 {

struct Region
{
    int low = 0, top = 127, root = 60;
    float tuneCents = 0.0f;
    int pan = 64;                    // 0..127, 64 = centre
    uint32_t loopStart = 0, loopEnd = 0;
    double rate = 44100.0;
    std::vector<float> data;         // mono

    bool hasLoop() const { return loopEnd > loopStart && loopEnd < data.size(); }
};

struct Instrument
{
    std::string name;
    std::vector<Region> regions;

    const Region* find(int note) const
    {
        for (const Region& r : regions)
            if (note >= r.low && note <= r.top) return &r;
        return nullptr;
    }
};

class SampleBank
{
public:
    static constexpr int    kMaxInstruments = 8;
    static constexpr size_t kMaxBytes = 48u * 1024u * 1024u;   // float samples across all instruments

    // Returns the number of instruments loaded (0 if Content/Instruments/index.txt is absent).
    int load()
    {
        instruments_.clear();
        bytes_ = 0;
        std::string indexText;
        if (!mpc::readTextFile(mpc::contentPath("Instruments/index.txt"), indexText)) return 0;
        std::istringstream index(indexText);
        std::string line;
        while ((int)instruments_.size() < kMaxInstruments && std::getline(index, line))
        {
            trim(line);
            if (line.empty() || line[0] == '#') continue;
            Instrument inst;
            if (loadMap("Instruments/" + line, inst)) instruments_.push_back(std::move(inst));
        }
        return (int)instruments_.size();
    }

    int count() const { return (int)instruments_.size(); }
    const Instrument* get(int index) const
    {
        return (index >= 0 && index < (int)instruments_.size()) ? &instruments_[(size_t)index] : nullptr;
    }
    size_t bytesUsed() const { return bytes_; }

private:
    static void trim(std::string& s)
    {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        s.erase(0, i);
    }

    bool loadMap(const std::string& relative, Instrument& inst)
    {
        std::string text;
        if (!mpc::readTextFile(mpc::contentPath(relative), text)) return false;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            trim(line);
            if (line.rfind("name ", 0) == 0) { inst.name = line.substr(5); continue; }
            if (line.rfind("region ", 0) != 0) continue;
            std::istringstream f(line.substr(7));
            std::string wavPath;
            Region r;
            int level = 0;
            long long loopStart = 0, loopEnd = 0;
            if (!(f >> wavPath >> r.low >> r.top >> r.root >> r.tuneCents >> level >> r.pan >> loopStart >> loopEnd)) continue;
            if (r.low < 0 || r.top > 127 || r.low > r.top) continue;
            dsp::WavData w;
            if (!dsp::loadWav(mpc::contentPath(wavPath), w) || w.numFrames() == 0) continue;
            const size_t frames = w.numFrames();
            if (bytes_ + frames * sizeof(float) > kMaxBytes) continue;   // skip, never overflow memory
            r.rate = w.sampleRate;
            r.data.resize(frames);
            const int nch = w.numChannels();
            for (size_t i = 0; i < frames; ++i)
            {
                float sum = 0.0f;
                for (int c = 0; c < nch; ++c) sum += w.channels[(size_t)c][i];
                r.data[i] = sum / (float)nch;
            }
            if (loopStart >= 0 && loopEnd > loopStart && (size_t)loopEnd < frames)
            {
                r.loopStart = (uint32_t)loopStart;
                r.loopEnd = (uint32_t)loopEnd;
            }
            bytes_ += frames * sizeof(float);
            inst.regions.push_back(std::move(r));
        }
        if (inst.name.empty()) inst.name = relative;
        return !inst.regions.empty();
    }

    std::vector<Instrument> instruments_;
    size_t bytes_ = 0;
};

// 4-point Hermite interpolation of a region at a fractional position, loop-aware.
inline float readInterpolated(const Region& r, double pos, bool loop)
{
    const long n = (long)r.data.size();
    const long i1 = (long)pos;
    const float t = (float)(pos - (double)i1);
    auto at = [&](long i) -> float
    {
        if (loop && i >= (long)r.loopEnd)
            i = (long)r.loopStart + (i - (long)r.loopEnd) % (long)(r.loopEnd - r.loopStart);
        if (i < 0) i = 0;
        if (i >= n) return 0.0f;
        return r.data[(size_t)i];
    };
    const float y0 = at(i1 - 1), y1 = at(i1), y2 = at(i1 + 1), y3 = at(i1 + 2);
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

} // namespace m3

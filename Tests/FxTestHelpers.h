#pragma once

// Stage 3 helpers: offline renders through single effects and through ShakalChain. Never makes a sound.

#include "Engine/Fx/ShakalChain.h"
#include "Engine/Fx/FxHash.h"
#include "Engine/Fx/MacroCurves.h"
#include "Engine/Transport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace jackal::test
{

using Signal = std::vector<float>;

inline Signal makeDcSignal (int n, float v) { return Signal (static_cast<std::size_t> (n), v); }

inline Signal makeSine (int n, double hz, double sr, float amp = 1.f)
{
    Signal s (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        s[static_cast<std::size_t> (i)] = amp * static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * hz * i / sr));
    return s;
}

inline Signal makeNoise (int n, uint32_t seed = 1, float amp = 1.f)
{
    Signal s (static_cast<std::size_t> (n));
    uint32_t x = seed != 0 ? seed : 1;
    for (int i = 0; i < n; ++i)
    {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        s[static_cast<std::size_t> (i)] = amp * (static_cast<float> (x) / 4294967296.f * 2.f - 1.f);
    }
    return s;
}

// -1..1 over n samples
inline Signal makeRampSignal (int n)
{
    Signal s (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        s[static_cast<std::size_t> (i)] = -1.f + 2.f * static_cast<float> (i) / static_cast<float> (std::max (1, n - 1));
    return s;
}

inline void expectBitEqual (const Signal& a, const Signal& b)
{
    REQUIRE (a.size() == b.size());
    std::size_t bad = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) { bad = i + 1; break; }
    INFO ("first difference at " << (bad == 0 ? -1 : static_cast<long long> (bad - 1)));
    REQUIRE (bad == 0);
}

inline void expectNoNanInf (const Signal& a)
{
    for (float v : a)
        REQUIRE (std::isfinite (v));
}

// Runs one effect in blocks of bufferSize; ctx.pos grows by n per block. Single control segment per block.
template <typename Fx>
std::pair<Signal, Signal> runFx (Fx& fx, const FxState& s, FxContext ctx, Signal l, Signal r, int bufferSize)
{
    const int total = static_cast<int> (l.size());
    for (int i = 0; i < total; i += bufferSize)
    {
        const int n = std::min (bufferSize, total - i);
        fx.process (l.data() + i, r.data() + i, n, s, ctx);
        ctx.pos += n;
    }
    return { std::move (l), std::move (r) };
}

// One ShakalChain driven like EngineCore drives it, with its own table and transport.
struct FxRig
{
    double sr;
    int chain;
    bool playing;
    ShakalChain fx;
    ParamTable table;
    Transport transport;
    uint32_t seed = 0x5A4C;
    int64_t pos = 0;
    std::vector<std::pair<int64_t, float>> triggers;

    explicit FxRig (double bpm = 120.0, bool play = true, int chainIndex = 0, double sampleRate = 48000.0, int maxBlock = 2048)
        : sr (sampleRate), chain (chainIndex), playing (play)
    {
        fx.prepare (sr, maxBlock, chain);
        transport.prepare (sr);
        transport.setBpm (bpm);
        if (playing)
            transport.play();
    }

    void setManual (FxParam p, float v) { table.set (fxParam (chain, p), v); }
    void setMacro (float v)
    {
        table.set (chain == kMasterChain ? masterParam (MasterParam::Shakal)
                   : chain == kGuitarChain ? guitarParam (GuitarParam::Shakal)
                                           : trackParam (chain, TrackParam::Shakal), v);
    }
    void setPreset (ShakalPreset p) { setManual (FxParam::Preset, static_cast<float> (p)); }
    void setBypass (bool b) { setManual (FxParam::Bypass, b ? 1.f : 0.f); }
    void trigger (int64_t sample, float macro) { triggers.push_back ({ sample, macro }); }

    // Continues from the current position. Per-block hook runs before each block.
    template <typename Hook>
    std::pair<Signal, Signal> render (Signal l, Signal r, int bufferSize, Hook&& hook)
    {
        const int total = static_cast<int> (l.size());
        for (int i = 0; i < total; i += bufferSize)
        {
            const int n = std::min (bufferSize, total - i);
            hook (pos, n);
            for (const auto& t : triggers)
                if (t.first >= pos && t.first < pos + n)
                    fx.onTrigger (static_cast<int> (t.first - pos), t.second);
            fx.process (l.data() + i, r.data() + i, n, table, transport, transport.positionSamples(), playing, seed);
            if (playing)
                transport.advance (n);
            pos += n;
        }
        return { std::move (l), std::move (r) };
    }

    std::pair<Signal, Signal> render (Signal l, Signal r, int bufferSize)
    {
        return render (std::move (l), std::move (r), bufferSize, [] (int64_t, int) {});
    }

    Signal renderMono (const Signal& in, int bufferSize) { return render (in, in, bufferSize).first; }
};

} // namespace jackal::test

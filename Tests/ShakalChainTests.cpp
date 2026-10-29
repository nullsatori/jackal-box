// Stage 3: ShakalChain and its place in EngineCore (offline renders only, no audio device).

#include "FxTestHelpers.h"
#include "RigHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

using namespace jackal;
using namespace jackal::test;
using Catch::Approx;

namespace
{
    constexpr double kSr = 48000.0;
    constexpr float kLock = 0.84f;   // K4: Iron still clips there (Wrap starts at 0.85), see DEVIATIONS.md

    // Glitch cell check against the formulas of contract §5.6 (cell length C at 120 BPM = 6000).
    GlitchBufferFx::Type checkCell (const Signal& in, const Signal& o, int c, uint32_t seed, int chain = 0, int C = 6000, int L = 2400)
    {
        const int s0 = c * C;
        const auto type = GlitchBufferFx::typeFor (FxHash::unit (seed, FxHash::kSaltGlitchType, chain, c));
        for (int k = 64; k < C - 64; ++k)
        {
            float f = 0.f;
            switch (type)
            {
                case GlitchBufferFx::Type::Stutter: f = in[static_cast<std::size_t> (s0 + (k % L))]; break;
                case GlitchBufferFx::Type::Reverse: f = in[static_cast<std::size_t> (s0 - 1 - k)]; break;
                case GlitchBufferFx::Type::Repeat:  f = in[static_cast<std::size_t> (s0 - C + k)]; break;
            }
            REQUIRE (o[static_cast<std::size_t> (s0 + k)] == f);
        }
        return type;
    }

    void allManualOn (FxRig& rig)
    {
        for (auto p : { FxParam::DriveOn, FxParam::Sp12On, FxParam::DecimOn, FxParam::CrushOn, FxParam::FlipOn, FxParam::GlitchOn })
            rig.setManual (p, 1.f);
        rig.setManual (FxParam::DriveDb, 12.f);
        rig.setManual (FxParam::Sp12FilterHz, 8000.f);
        rig.setManual (FxParam::DecimRateHz, 8000.f);
        rig.setManual (FxParam::CrushBits, 6.f);
        rig.setManual (FxParam::FlipProb, 0.01f);
        rig.setManual (FxParam::FlipBit, 10.f);
        rig.setManual (FxParam::GlitchProb, 1.f);
        rig.setManual (FxParam::GlitchLenMs, 30.f);
    }

    double mean (const Signal& s, int64_t a, int64_t b)
    {
        double sum = 0;
        for (int64_t i = a; i < b; ++i)
            sum += s[static_cast<std::size_t> (i)];
        return sum / static_cast<double> (b - a);
    }

    // Same cell walk as ShakalChain, for the reference chain of K2.
    void cellOf (const Transport& t, int64_t pos, int64_t& c, int64_t& start, int64_t& end)
    {
        auto at = [&t] (int64_t x) { return t.sampleAtBeat (static_cast<double> (x) / 4.0); };
        c = static_cast<int64_t> (std::floor (t.beatAtSample (pos) * 4.0));
        while (at (c) > pos) --c;
        while (at (c + 1) <= pos) ++c;
        start = at (c);
        end = at (c + 1);
    }
} // namespace

TEST_CASE ("K1: bypass and neutral are bit-exact", "[chain]")
{
    const auto in = makeNoise (48000, 21);
    for (int buffer : { 64, 333 })
    {
        FxRig rig;
        rig.setPreset (ShakalPreset::Iron);
        rig.setMacro (0.8f);
        allManualOn (rig);
        rig.setBypass (true);
        expectBitEqual (rig.renderMono (in, buffer), in);

        FxRig neutral;
        expectBitEqual (neutral.renderMono (in, buffer), in);
    }

    // ring keeps being written under bypass
    const auto in2 = makeNoise (96000, 22);
    FxRig rig;
    rig.setPreset (ShakalPreset::Mem);
    rig.setManual (FxParam::GlitchOn, 1.f);
    rig.setManual (FxParam::GlitchProb, 1.f);
    rig.setManual (FxParam::GlitchLenMs, 50.f);
    const auto out = rig.render (in2, in2, 250, [&] (int64_t pos, int) { rig.setBypass (pos < 48000); }).first;
    for (int i = 0; i < 48000; ++i)
        REQUIRE (out[static_cast<std::size_t> (i)] == in2[static_cast<std::size_t> (i)]);
    bool sawReverse = false;
    for (int c = 8; c < 16; ++c)
        sawReverse = checkCell (in2, out, c, rig.seed) == GlitchBufferFx::Type::Reverse || sawReverse;
    REQUIRE (sawReverse);
}

TEST_CASE ("K2: fixed effect order", "[chain]")
{
    const int n = 48000;
    const auto in = makeNoise (n, 23);

    FxRig rig (172.0);
    for (auto p : { FxParam::DriveOn, FxParam::Sp12On, FxParam::DecimOn, FxParam::CrushOn, FxParam::FlipOn, FxParam::GlitchOn })
        rig.setManual (p, 1.f);
    rig.setManual (FxParam::DriveDb, 12.f);
    rig.setManual (FxParam::Sp12FilterHz, 20000.f);
    rig.setManual (FxParam::DecimRateHz, 6000.f);
    rig.setManual (FxParam::CrushBits, 5.f);
    rig.setManual (FxParam::FlipProb, 0.2f);
    rig.setManual (FxParam::FlipBit, 11.f);
    rig.setManual (FxParam::GlitchProb, 1.f);
    rig.setManual (FxParam::GlitchLenMs, 30.f);
    const auto chained = rig.render (in, in, 128);
    const FxState state = manualFxState (rig.table, 0);

    // reference: the six effects one after another over the whole signal
    enum E { D, S, De, Cr, Fl };
    auto reference = [&] (std::vector<int> order) {
        Signal l = in, r = in;
        FxContext c;
        c.sampleRate = kSr;
        c.seed = rig.seed;
        c.playing = true;
        c.driveGainStart = c.driveGainEnd = std::pow (10.f, 12.f / 20.f);
        DriveFx d; Sp12Fx s; DecimatorFx de; BitcrushFx cr; BitflipFx fl; GlitchBufferFx g;
        s.prepare (kSr, n); de.prepare (kSr, n); g.prepare (kSr, 128);
        for (int e : order)
            switch (e)
            {
                case D:  d.process (l.data(), r.data(), n, state, c); break;
                case S:  s.process (l.data(), r.data(), n, state, c); break;
                case De: de.process (l.data(), r.data(), n, state, c); break;
                case Cr: cr.process (l.data(), r.data(), n, state, c); break;
                case Fl: fl.process (l.data(), r.data(), n, state, c); break;
                default: break;
            }
        Transport t;
        t.prepare (kSr);
        t.setBpm (172.0);
        t.play();
        for (int64_t p = 0; p < n;)
        {
            int64_t ci = 0, cs = 0, ce = 0;
            cellOf (t, p, ci, cs, ce);
            const int m = static_cast<int> (std::min<int64_t> (n - p, ce - p));
            c.pos = p; c.cellIndex = ci; c.cellStartSample = cs; c.cellLength = static_cast<int> (ce - cs);
            g.process (l.data() + p, r.data() + p, m, state, c);
            p += m;
        }
        return std::make_pair (l, r);
    };

    const auto ref = reference ({ D, S, De, Cr, Fl });
    expectBitEqual (chained.first, ref.first);
    expectBitEqual (chained.second, ref.second);

    // swapping two neighbours changes the result (Crush <-> Flip; Crush and Decim commute, see DEVIATIONS.md)
    REQUIRE (reference ({ D, S, De, Fl, Cr }).first != chained.first);
}

TEST_CASE ("K3: macro smoothing and drive gain ramp", "[chain]")
{
    FxRig rig;
    rig.setPreset (ShakalPreset::Iron);
    const int jumpBlock = 150;
    std::vector<float> actual;
    Signal in = makeDcSignal (32 * 400, 0.1f);
    const auto out = rig.render (in, in, 32, [&] (int64_t pos, int) {
        if (pos > 0) actual.push_back (rig.fx.macroActual());
        rig.setMacro (pos >= jumpBlock * 32 ? 1.f : 0.f);
    }).first;
    actual.push_back (rig.fx.macroActual());

    // actual[i] = value after block i
    REQUIRE (actual[jumpBlock - 1] == 0.f);
    REQUIRE (actual[jumpBlock] < 0.5f);
    for (std::size_t i = jumpBlock + 1; i < actual.size(); ++i)
        REQUIRE (actual[i] >= actual[i - 1]);
    REQUIRE (actual[jumpBlock + 14] >= 0.99f);

    float maxStep = 0.f;
    for (std::size_t i = 1; i < out.size(); ++i)
    {
        if (std::fabs (out[i]) >= 0.999f || actual[i / 32] >= 0.8f)
            break;
        if (actual[(i - 1) / 32] < 0.55f)
            continue;
        maxStep = std::max (maxStep, std::fabs (out[i] - out[i - 1]));
    }
    INFO ("largest step before the clip " << maxStep);
    REQUIRE (maxStep <= 1.f / 32.f + 1e-3f);   // one 6-bit crush step, see DEVIATIONS.md (stage 3, K3)
}

TEST_CASE ("K4: p-lock of the macro", "[chain]")
{
    const double panGain = std::cos (3.14159265358979323846 / 4.0);   // centre pan of the voice
    constexpr int64_t step = 6000;                                       // 1/16 at 120 BPM
    const int64_t ms15 = 720;

    auto setup = [] (OfflineRig& rig, bool plocks) {
        rig.bpm (120.0);
        rig.masterGainDb (0.f);
        rig.setSample (0, makeDc (48000, 0.3f));
        rig.setTrack (0, 0);
        Pattern p;
        auto& lane = p.lanes[0];
        lane.length = 16;
        for (int s : { 0, 4, 8 })
        {
            lane.steps[static_cast<std::size_t> (s)].active = true;
            lane.steps[static_cast<std::size_t> (s)].velocity = 127;
        }
        if (plocks)
            for (int s : { 0, 8 })
                lane.steps[static_cast<std::size_t> (s)].plocks[0] = { static_cast<uint8_t> (TrackParam::Shakal), kLock };
        rig.setPattern (0, p);
        rig.play();
    };

    auto run = [&] (int buffer) {
        OfflineRig rig (buffer);
        setup (rig, true);
        rig.renderUntil (12 * step);
        return rig.left;
    };

    const auto base = run (128);
    const double low = 0.3 * panGain;
    REQUIRE (mean (base, ms15, 4 * step) > 0.95);
    REQUIRE (mean (base, 4 * step + ms15, 8 * step) == Approx (low).margin (0.01));
    REQUIRE (mean (base, 8 * step + ms15, 8 * step + 19200) > 0.95);

    for (int buffer : { 64, 1024, 7 })
        expectBitEqual (run (buffer), base);

    // the knob moves during the hold: nothing changes until the next plain trigger
    {
        OfflineRig rig (128);
        setup (rig, true);
        rig.renderUntil (8 * step, [&] (int64_t pos) { if (pos >= 2 * step) rig.param (0, TrackParam::Shakal, 0.5f); });
        for (int64_t i = 0; i < 4 * step; ++i)
            REQUIRE (rig.left[static_cast<std::size_t> (i)] == base[static_cast<std::size_t> (i)]);

        OfflineRig ref (128);
        setup (ref, false);
        ref.param (0, TrackParam::Shakal, 0.5f);
        ref.renderUntil (8 * step);
        REQUIRE (mean (rig.left, 4 * step + ms15, 8 * step) == Approx (mean (ref.left, 4 * step + ms15, 8 * step)).margin (0.01));
    }

    // a live hit during the hold clears the override; Stop clears it too
    {
        OfflineRig rig (128);
        setup (rig, true);
        rig.renderUntil (2 * step);
        REQUIRE ((rig.core.shakalSnapshot().overrideMask & 1u) != 0);
        rig.cmd (CommandType::TriggerTrack, 0, 127.f);
        rig.renderUntil (4 * step);
        REQUIRE (mean (rig.left, 2 * step + ms15, 4 * step) == Approx (low).margin (0.01));
        REQUIRE ((rig.core.shakalSnapshot().overrideMask & 1u) == 0);

        rig.renderUntil (9 * step);
        REQUIRE ((rig.core.shakalSnapshot().overrideMask & 1u) != 0);
        rig.cmd (CommandType::Stop);
        rig.renderUntil (9 * step + 128);
        REQUIRE (rig.core.shakalSnapshot().overrideMask == 0);
    }
}

TEST_CASE ("K5: buffer independence across the whole engine", "[chain]")
{
    auto project = makeDemoProject (kSr);
    const float macros[] = { 0.3f, 0.6f, 0.9f, 1.0f, 0.5f, 0.7f, 0.2f, 1.0f };
    for (int t = 0; t < kNumTracks; ++t)
        project->tracks[static_cast<std::size_t> (t)].params[static_cast<std::size_t> (TrackParam::Shakal)] = macros[t];
    int locks = 0;
    for (auto& s : project->patterns[0].lanes[7].steps)
        if (s.active && locks < 3)
        {
            s.plocks[0] = { static_cast<uint8_t> (TrackParam::Shakal), locks == 1 ? 0.f : 1.f };
            ++locks;
        }
    REQUIRE (locks > 0);

    auto render = [&] (int buffer) {
        OfflineRig rig (buffer);
        rig.loadProject (*project);
        for (int t = 0; t < kNumTracks; ++t)
            rig.core.params().params.set (fxParam (t, FxParam::Preset), static_cast<float> (t % 3));
        rig.core.params().params.set (fxParam (kMasterChain, FxParam::Preset), static_cast<float> (ShakalPreset::Net));
        rig.core.params().params.set (masterParam (MasterParam::Shakal), 0.5f);
        rig.play();
        rig.renderSeconds (8.0 * 4.0 * 60.0 / 172.0);
        return rig.left;
    };

    const auto a = render (128);
    expectNoNanInf (a);
    float peak = 0.f;
    for (float v : a)
        peak = std::max (peak, std::fabs (v));
    REQUIRE (peak > 0.01f);
    REQUIRE (peak <= std::pow (10.f, project->masterGainDb / 20.f) + 1e-3f);
    for (int buffer : { 32, 64, 256, 512, 1024, 7, 333 })
        expectBitEqual (render (buffer), a);

    // hot DC and silence through every chain
    for (int c = 0; c < kNumChains; ++c)
    {
        FxRig rig (172.0, true, c);
        rig.setMacro (1.f);
        Signal x = makeDcSignal (24000, 10.f);
        const auto neg = makeDcSignal (24000, -10.f);
        x.insert (x.end(), neg.begin(), neg.end());
        x.resize (x.size() + 24000, 0.f);
        expectNoNanInf (rig.renderMono (x, 256));

        // denormals do not live on: a second of silence after noise ends in exact zeros (flip stays off below 0.8)
        FxRig quiet (172.0, true, c);
        quiet.setMacro (0.75f);
        Signal y = makeNoise (24000, 30);
        y.resize (y.size() + 48000, 0.f);
        const auto o = quiet.renderMono (y, 256);
        for (std::size_t i = o.size() - 1000; i < o.size(); ++i)
            REQUIRE (o[i] == 0.f);
    }
}

TEST_CASE ("K6: nine full chains stay cheap", "[chain][cpu]")
{
#if defined(JACKAL_UNDER_SANITIZER) || ! defined(NDEBUG)
    SKIP ("CPU budget is measured in optimised builds without sanitizers");
#else
    std::vector<std::unique_ptr<FxRig>> rigs;
    for (int c = 0; c < kNumChains; ++c)
    {
        rigs.push_back (std::make_unique<FxRig> (172.0, true, c, kSr, 128));
        allManualOn (*rigs.back());
    }
    const auto noise = makeNoise (128, 31);
    Signal l, r;
    double seconds = 0.0;
    const int blocks = static_cast<int> (10.0 * kSr / 128.0);
    for (int b = 0; b < blocks; ++b)
        for (auto& rig : rigs)
        {
            l = noise;
            r = noise;
            const auto t0 = std::chrono::steady_clock::now();
            rig->fx.process (l.data(), r.data(), 128, rig->table, rig->transport, rig->transport.positionSamples(), true, rig->seed);
            seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            rig->transport.advance (128);
        }
    INFO ("nine chains, 10 s of audio: " << seconds << " s");
    REQUIRE (seconds < 2.0);
#endif
}

TEST_CASE ("K7: chains run with the transport stopped", "[chain]")
{
    auto run = [] (OfflineRig& rig) {
        rig.left.clear();
        rig.right.clear();
        rig.cmd (CommandType::TriggerTrack, 0, 127.f);
        rig.renderSeconds (1.0);
        return rig.left;
    };
    auto setup = [] (OfflineRig& rig) {
        rig.masterGainDb (0.f);
        rig.setSample (0, makeRamp (24000));
        rig.setTrack (0, 0);
        rig.param (0, TrackParam::Shakal, 1.f);
        rig.core.params().params.set (fxParam (0, FxParam::Preset), static_cast<float> (ShakalPreset::Mem));
    };

    OfflineRig a (128), b (128);
    setup (a);
    setup (b);
    const auto first = run (a);
    expectNoNanInf (first);
    expectBitEqual (run (b), first);

    a.cmd (CommandType::Play);
    a.cmd (CommandType::Stop);
    expectBitEqual (run (a), first);

    // stopped cells are sr/8 long: at 48 kHz that is the 1/16 cell of 120 BPM, so both renders match
    const auto in = makeNoise (48000, 32);
    auto glitchOnly = [&] (bool playing) {
        FxRig rig (120.0, playing);
        rig.setManual (FxParam::GlitchOn, 1.f);
        rig.setManual (FxParam::GlitchProb, 1.f);
        return rig.renderMono (in, 100);
    };
    const auto stopped = glitchOnly (false);
    REQUIRE (stopped != in);
    expectBitEqual (stopped, glitchOnly (true));
}

TEST_CASE ("E2: default chains change nothing", "[chain]")
{
    auto project = makeDemoProject (kSr);
    auto render = [&] (bool disable) {
        OfflineRig rig (256);
        rig.core.setFxDisabledForTests (disable);
        rig.loadProject (*project);
        rig.play();
        rig.renderSeconds (8.0 * 4.0 * 60.0 / 172.0);
        return rig.left;
    };
    expectBitEqual (render (false), render (true));
}

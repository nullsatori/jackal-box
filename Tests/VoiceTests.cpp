#include "RigHelpers.h"

#include "Engine/TrackPlayer.h"
#include "Engine/Voice.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using namespace jackal;
using namespace jackal::test;

namespace
{
    struct Out
    {
        std::vector<float> l, r;
        explicit Out (std::size_t n) : l (n, 0.f), r (n, 0.f) {}
    };

    TriggerParams exact (const Sample* s, int64_t start, int64_t end)
    {
        TriggerParams p;
        p.sample = s;
        p.startSample = start;
        p.endSample = end;
        p.pan = -1.f;          // left gain exactly 1
        p.attackSamples = 0;   // bit-exact read-out (see DEVIATIONS.md, stage 2)
        return p;
    }

    int64_t runLength (const std::vector<float>& x, std::size_t from = 0)
    {
        std::size_t n = 0;
        while (from + n < x.size() && x[from + n] != 0.f)
            ++n;
        return static_cast<int64_t> (n);
    }
} // namespace

TEST_CASE ("V1: slices read exactly their range", "[voice]")
{
    std::unique_ptr<Sample> s (makeRamp (4800));
    s->sliceStarts.clear();
    for (int i = 0; i <= 16; ++i)
        s->sliceStarts.push_back (300 * i);

    for (int k = 0; k < 16; ++k)
    {
        Voice v;
        v.prepare (48000.0);
        v.start (exact (s.get(), 300 * k, 300 * k + 300));
        Out o (400);
        v.render (o.l.data(), o.r.data(), 0, 400);
        for (int i = 0; i < 300; ++i)
            REQUIRE (o.l[static_cast<std::size_t> (i)] == s->data[0][static_cast<std::size_t> (300 * k + i)]);
        REQUIRE (o.l[300] == 0.f);
        REQUIRE (o.r[0] == 0.f);
    }
    REQUIRE (s->sliceStarts.back() == s->length);
}

TEST_CASE ("V2: reverse and Start/End", "[voice]")
{
    std::unique_ptr<Sample> s (makeRamp (4800));
    Voice v;
    v.prepare (48000.0);
    auto p = exact (s.get(), 600, 900);
    p.reverse = true;
    v.start (p);
    Out o (400);
    v.render (o.l.data(), o.r.data(), 0, 400);
    for (int i = 0; i < 300; ++i)
        REQUIRE (o.l[static_cast<std::size_t> (i)] == s->data[0][static_cast<std::size_t> (899 - i)]);
    REQUIRE (o.l[300] == 0.f);

    // Start/End through the sequencer on an unsliced track
    auto pat = std::make_unique<Pattern>();
    pat->lanes[0].length = 64;
    pat->lanes[0].steps[0].active = true;

    for (auto [st, en, expected] : { std::tuple { 0.25f, 0.5f, int64_t (1200) }, std::tuple { 0.5f, 0.25f, int64_t (2400) } })
    {
        OfflineRig rig (128);
        rig.setSample (0, makeRamp (4800));
        rig.setTrack (0, 0);
        rig.param (0, TrackParam::Start, st);
        rig.param (0, TrackParam::End, en);
        rig.setPattern (0, *pat);
        rig.play();
        rig.renderUntil (8000);
        REQUIRE (runLength (rig.left) == expected);
    }
}

TEST_CASE ("V3: pitch, source rate and fit to tempo", "[voice]")
{
    std::unique_ptr<Sample> s (makeRamp (4800));
    {
        Voice v;
        v.prepare (48000.0);
        auto p = exact (s.get(), 0, 4800);
        p.pitchSemitones = 12.f;
        v.start (p);
        Out o (6000);
        v.render (o.l.data(), o.r.data(), 0, 6000);
        REQUIRE (std::llabs (runLength (o.l) - 2400) <= 2);
    }
    {
        std::unique_ptr<Sample> s44 (makeRamp (4410, 44100.0));
        Voice v;
        v.prepare (48000.0);
        v.start (exact (s44.get(), 0, 4410));
        Out o (6000);
        v.render (o.l.data(), o.r.data(), 0, 6000);
        REQUIRE (std::llabs (runLength (o.l) - 4800) <= 2);
    }
    {
        auto pat = std::make_unique<Pattern>();
        pat->lanes[0].length = 32;
        pat->lanes[0].steps[0].active = true;

        auto* loop = makeRamp (48000);
        loop->sourceBars = 1.0;
        OfflineRig rig (128);
        rig.setSample (0, loop);
        rig.setTrack (0, 0, 0, true);
        rig.setPattern (0, *pat);
        rig.bpm (120.0);
        rig.play();
        rig.renderSeconds (2.5);
        REQUIRE (std::llabs (runLength (rig.left) - 96000) <= 2);
    }
}

TEST_CASE ("V4: decay, tone and pan", "[voice]")
{
    std::unique_ptr<Sample> dc (makeDc (48000));
    {
        Voice v;
        v.prepare (48000.0);
        auto p = exact (dc.get(), 0, 48000);
        p.decayMs = 100.f;
        v.start (p);
        Out o (6000);
        v.render (o.l.data(), o.r.data(), 0, 6000);
        const double db = 20.0 * std::log10 (static_cast<double> (o.l[4800]) / static_cast<double> (o.l[0]));
        REQUIRE (std::fabs (db + 60.0) <= 1.0);
    }
    {
        Voice v;
        v.prepare (48000.0);
        auto p = exact (dc.get(), 0, 48000);
        p.toneHz = 1000.f;
        v.start (p);
        Out o (4000);
        v.render (o.l.data(), o.r.data(), 0, 4000);
        REQUIRE (std::fabs (o.l[3999] - 1.f) < 1e-3f);
    }
    {
        auto noise = std::make_unique<Sample>();
        noise->length = 48000;
        noise->data[0].resize (48000);
        uint32_t x = 12345;
        for (auto& v : noise->data[0])
        {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            v = static_cast<float> (static_cast<double> (x) / 4294967296.0 * 2.0 - 1.0);
        }
        auto hfEnergy = [&] (float tone) {
            Voice v;
            v.prepare (48000.0);
            auto p = exact (noise.get(), 0, 48000);
            p.toneHz = tone;
            v.start (p);
            Out o (48000);
            v.render (o.l.data(), o.r.data(), 0, 48000);
            double e = 0.0;
            for (std::size_t i = 1; i < o.l.size(); ++i)
                e += std::pow (static_cast<double> (o.l[i] - o.l[i - 1]), 2.0);   // first difference ~ highs
            return e;
        };
        REQUIRE (hfEnergy (1000.f) < 0.25 * hfEnergy (20000.f));
    }
    {
        Voice v;
        v.prepare (48000.0);
        auto p = exact (dc.get(), 0, 1000);
        v.start (p);
        Out o (100);
        v.render (o.l.data(), o.r.data(), 0, 100);
        REQUIRE (o.r[50] == 0.f);

        p.pan = 0.f;
        v.start (p);
        Out c (100);
        v.render (c.l.data(), c.r.data(), 0, 100);
        REQUIRE (c.l == c.r);
    }
}

TEST_CASE ("V5: retrigger declick and choke groups", "[voice]")
{
    std::unique_ptr<Sample> dc (makeDc (48000));
    {
        TrackPlayer tp;
        tp.prepare (48000.0, 2048);
        TriggerParams p;
        p.sample = dc.get();
        p.endSample = 48000;
        p.pan = -1.f;
        tp.trigger (0, p);
        tp.trigger (500, p);
        Out o (2048);
        tp.render (o.l.data(), o.r.data(), 2048);
        float maxJump = 0.f;
        for (std::size_t i = 1; i < o.l.size(); ++i)
            maxJump = std::max (maxJump, std::fabs (o.l[i] - o.l[i - 1]));
        REQUIRE (maxJump <= 0.05f);
        REQUIRE (o.l[2000] == 1.f);
    }

    for (bool sameGroup : { true, false })
    {
        auto pat = std::make_unique<Pattern>();
        pat->lanes[1].length = 64;
        pat->lanes[1].steps[0].active = true;   // OH at 0
        pat->lanes[0].length = 64;
        pat->lanes[0].steps[1].active = true;   // CH one 16th later

        OfflineRig rig (128);
        rig.setSample (0, makeDc (96000, 0.5f));
        rig.setTrack (0, 0, 1);
        rig.setTrack (1, 0, sameGroup ? 1 : 0);
        rig.param (0, TrackParam::Pan, 1.f);
        rig.param (1, TrackParam::Pan, -1.f);
        rig.setPattern (0, *pat);
        rig.play();
        rig.renderSeconds (0.5);

        const auto ch = static_cast<std::size_t> (std::llround (48000.0 * 60.0 / 172.0 / 4.0));
        REQUIRE (rig.left[ch - 1] != 0.f);
        if (sameGroup)
            REQUIRE (rig.left[ch + 64] == 0.f);
        else
            REQUIRE (rig.left[ch + 64] != 0.f);
    }
}

#include "RigHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace jackal;
using namespace jackal::test;

namespace
{
    constexpr double kBpm = 120.0;
    constexpr int64_t kSpb = 24000;   // 120 BPM at 48 kHz

    Sample* impulseTrain (int64_t length, int64_t period)
    {
        auto* s = new Sample();
        s->length = length;
        s->data[0].assign (static_cast<std::size_t> (length), 0.f);
        for (int64_t i = 0; i < length; i += period)
            s->data[0][static_cast<std::size_t> (i)] = 1.f;
        return s;
    }

    struct PadRun { int pad = -1; int64_t down = -1, up = -1; };

    std::vector<float> run (Sample* source, PadRun pr, int64_t total, int buffer = 128)
    {
        auto pat = std::make_unique<Pattern>();
        pat->lanes[0].length = 64;
        pat->lanes[0].steps[0].active = true;

        OfflineRig rig (buffer);
        rig.setSample (0, source);
        rig.setTrack (0, 0);
        rig.param (0, TrackParam::Pan, -1.f);
        rig.masterGainDb (0.f);
        rig.setPattern (0, *pat);
        rig.bpm (kBpm);
        rig.play();
        if (pr.down >= 0)
        {
            rig.renderUntil (pr.down);
            rig.cmd (CommandType::PadDown, pr.pad);
        }
        if (pr.up >= 0)
        {
            rig.renderUntil (pr.up);
            rig.cmd (CommandType::PadUp, pr.pad);
        }
        rig.renderUntil (total);
        return rig.left;
    }
} // namespace

TEST_CASE ("G1: stutter 1/16 repeats the window and lets go cleanly", "[glitch]")
{
    const int64_t total = 40000;
    const auto dry = run (impulseTrain (96000, 1000), {}, total);
    const auto wet = run (impulseTrain (96000, 1000), { 0, 1234, 30000 }, total);

    const int64_t a = 6000, len = 6000;
    for (int64_t i = 0; i < a; ++i)
        REQUIRE (wet[static_cast<std::size_t> (i)] == dry[static_cast<std::size_t> (i)]);
    for (int m = 0; m < 4; ++m)
        for (int64_t i = 0; i < len; ++i)
            REQUIRE (wet[static_cast<std::size_t> (a + m * len + i)] == dry[static_cast<std::size_t> (a + i)]);
    for (int64_t i = 30000 + 64; i < total; ++i)
        REQUIRE (wet[static_cast<std::size_t> (i)] == dry[static_cast<std::size_t> (i)]);
}

TEST_CASE ("G2: reverse, gate, crush, tape stop, pitch dive", "[glitch]")
{
    const int64_t a = 6000;

    {
        const auto dry = run (makeRamp (96000), {}, 20000);
        const auto wet = run (makeRamp (96000), { 3, 1234, -1 }, 20000);
        for (int64_t i = 64; i < 5000; ++i)
            REQUIRE (wet[static_cast<std::size_t> (a + i)] == dry[static_cast<std::size_t> (a - 1 - i)]);
    }
    {
        const auto wet = run (makeDc (96000, 0.5f), { 6, 1234, -1 }, 20000);
        const int64_t l32 = 3000;
        for (int c = 0; c < 4; ++c)
            for (int64_t i = l32 / 2 + 2; i < l32 - 2; ++i)
                REQUIRE (wet[static_cast<std::size_t> (a + c * l32 + i)] == 0.f);
        REQUIRE (wet[static_cast<std::size_t> (a + 100)] != 0.f);
    }
    {
        const auto wet = run (makeRamp (96000), { 5, 1234, -1 }, 20000);
        for (int64_t t = a + 64; t < a + 4000; ++t)
        {
            const float v = wet[static_cast<std::size_t> (t)];
            REQUIRE (std::fabs (v * 32.f - std::round (v * 32.f)) < 1e-4f);
            REQUIRE (v == wet[static_cast<std::size_t> (a + ((t - a) / 8) * 8)]);
        }
    }
    {
        const auto wet = run (makeDc (200000, 0.5f), { 4, 1234, -1 }, a + 2 * kSpb + 4000);
        REQUIRE (wet[static_cast<std::size_t> (a + 100)] != 0.f);
        for (int64_t t = a + 2 * kSpb; t < a + 2 * kSpb + 4000; ++t)
            REQUIRE (wet[static_cast<std::size_t> (t)] == 0.f);
    }
    {
        const auto wet = run (makeRamp (200000), { 7, 1234, -1 }, a + 6 * kSpb);
        for (float v : wet)
            REQUIRE (std::isfinite (v));
    }
}

TEST_CASE ("G3: the glitch lane plays and records pads", "[glitch]")
{
    auto pat = std::make_unique<Pattern>();
    for (int c = 4; c <= 7; ++c)
        pat->glitch.pad[static_cast<std::size_t> (c)] = 1;

    OfflineRig rig (64);
    rig.setPattern (0, *pat);
    rig.bpm (kBpm);
    rig.play();
    rig.renderUntil (kSpb / 2);
    REQUIRE (rig.core.sequencerSnapshot().activePad == -1);
    rig.renderUntil (kSpb + kSpb / 2);
    REQUIRE (rig.core.sequencerSnapshot().activePad == 0);
    rig.renderUntil (2 * kSpb + kSpb / 2);
    REQUIRE (rig.core.sequencerSnapshot().activePad == -1);

    // recording a held pad writes every cell it passes
    OfflineRig rec (64);
    rec.setPattern (0, Pattern {});
    rec.bpm (kBpm);
    rec.cmd (CommandType::SetRecording, 1);
    rec.play();
    rec.renderUntil (64);
    rec.cmd (CommandType::PadDown, 2);
    rec.renderUntil (kSpb);   // cells 1, 2, 3
    rec.cmd (CommandType::PadUp, 2);
    rec.renderUntil (2 * kSpb);

    std::vector<int> cells;
    for (const auto& e : rec.events)
        if (e.type == EngineEventType::GlitchRecorded)
        {
            REQUIRE (e.value == 3);
            cells.push_back (e.step);
        }
    REQUIRE (cells == std::vector<int> { 1, 2, 3 });
}

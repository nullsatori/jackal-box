// Stage 4: guitar strip gate, low cut and defaults (inputs are numbers; offline only).

#include "LoopTestHelpers.h"

#include "Engine/InputMonitor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace jackal;
using namespace jackal::test;

namespace
{
    constexpr double kSr = 48000.0;

    Signal runStrip (const Signal& in, float gateDb, float hpfHz, int buffer = 128)
    {
        OfflineRig rig (buffer);
        rig.core.params().monitorEnabled = true;
        rig.core.params().params.set (guitarParam (GuitarParam::GateDb), gateDb);
        rig.core.params().params.set (guitarParam (GuitarParam::HpfHz), hpfHz);
        rig.input = [&in] (int64_t pos, int n, float* buf) {
            for (int i = 0; i < n; ++i)
                buf[i] = static_cast<std::size_t> (pos + i) < in.size() ? in[static_cast<std::size_t> (pos + i)] : 0.f;
        };
        rig.renderUntil (static_cast<int64_t> (in.size()));
        return rig.left;
    }

    double rms (const Signal& s, std::size_t from, std::size_t to)
    {
        double acc = 0.0;
        for (std::size_t i = from; i < to; ++i)
            acc += static_cast<double> (s[i]) * s[i];
        return std::sqrt (acc / static_cast<double> (to - from));
    }
}

TEST_CASE ("G1: noise gate", "[guitar]")
{
    const int half = static_cast<int> (kSr / 2);
    Signal in = makeSine (half, 220.0, kSr, 0.5f);
    const Signal quiet = makeSine (half, 220.0, kSr, 0.001f);
    in.insert (in.end(), quiet.begin(), quiet.end());

    const Signal out = runStrip (in, -40.f, 20.f);
    for (int i = static_cast<int> (0.005 * kSr); i < half; ++i)
        REQUIRE (std::fabs (out[static_cast<std::size_t> (i)] - in[static_cast<std::size_t> (i)]) <= 1e-6f);
    for (int i = half + static_cast<int> (0.1 * kSr); i < 2 * half; ++i)
        REQUIRE (out[static_cast<std::size_t> (i)] == 0.f);

    const Signal noise = makeNoise (half, 3, 0.2f);
    expectBitEqual (runStrip (noise, -80.f, 20.f), noise);
}

TEST_CASE ("G2: low cut", "[guitar]")
{
    const int n = static_cast<int> (kSr);
    const auto from = static_cast<std::size_t> (0.1 * kSr);
    const auto to = static_cast<std::size_t> (n);

    const Signal low = makeSine (n, 50.0, kSr, 0.5f);
    const Signal lowOut = runStrip (low, -80.f, 100.f);
    REQUIRE (20.0 * std::log10 (rms (lowOut, from, to) / rms (low, from, to)) <= -10.0);

    const Signal high = makeSine (n, 1000.0, kSr, 0.5f);
    const Signal highOut = runStrip (high, -80.f, 100.f);
    REQUIRE (std::fabs (20.0 * std::log10 (rms (highOut, from, to) / rms (high, from, to))) <= 0.5);

    expectBitEqual (runStrip (low, -80.f, 20.f), low);

    const Signal dc = makeDcSignal (n, 0.5f);
    const Signal dcOut = runStrip (dc, -80.f, 300.f);
    REQUIRE (std::fabs (dcOut.back()) < 1e-4f);
}

TEST_CASE ("G3: strip defaults equal the stage 1 monitor", "[guitar]")
{
    const int n = static_cast<int> (kSr / 2);
    const Signal noise = makeNoise (n, 11, 0.7f);
    const Signal out = runStrip (noise, kGuitarParamDefaults[1], kGuitarParamDefaults[2]);

    InputMonitor monitor;
    monitor.prepare (kSr);
    Signal want (noise.size(), 0.f);
    for (int i = 0; i < n; i += 128)
    {
        const int len = std::min (128, n - i);
        const float* in[1] = { noise.data() + i };
        float* o[1] = { want.data() + i };
        monitor.process (in, 1, o, 1, len, 1.f, true);
    }
    expectBitEqual (out, want);

    // monitor off: the live input is gone, the loop still plays
    OfflineRig rig (128);
    rig.core.params().monitorEnabled = false;
    rig.input = [&noise] (int64_t pos, int len, float* buf) {
        for (int i = 0; i < len; ++i)
            buf[i] = static_cast<std::size_t> (pos + i) < noise.size() ? noise[static_cast<std::size_t> (pos + i)] : 0.f;
    };
    loopPattern (rig, 0, 8);
    rig.bpm (120.0);
    auto* loop = makeRamp (kLoopBar);
    loop->sourceBars = 1.0;
    rig.setSample (8, loop);
    rig.play();
    rig.renderUntil (kLoopBar);
    for (int64_t i = 0; i < kLoopBar; ++i)
        REQUIRE (rig.left[static_cast<std::size_t> (i)] == loop->data[0][static_cast<std::size_t> (i)]);
}

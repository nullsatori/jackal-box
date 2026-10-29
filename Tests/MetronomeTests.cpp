#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace jackal;
using namespace jackal::test;

namespace
{
    std::vector<int64_t> analyticClicks (double bpm, double sampleRate, double seconds)
    {
        const double spb = sampleRate * 60.0 / bpm;
        const auto total = static_cast<int64_t> (std::llround (seconds * sampleRate));
        std::vector<int64_t> clicks;
        for (int64_t k = 0;; ++k)
        {
            const auto s = static_cast<int64_t> (std::llround (static_cast<double> (k) * spb));
            if (s >= total)
                break;
            clicks.push_back (s);
        }
        return clicks;
    }

    int zeroCrossings (const std::vector<float>& data, int64_t start, int length)
    {
        int count = 0;
        for (int64_t i = start + 1; i < start + length && i < static_cast<int64_t> (data.size()); ++i)
            if ((data[static_cast<std::size_t> (i - 1)] >= 0.f) != (data[static_cast<std::size_t> (i)] >= 0.f))
                ++count;
        return count;
    }
} // namespace

TEST_CASE ("T1: click indices are identical for every buffer size and match the formula", "[metronome]")
{
    const auto expected = analyticClicks (172.0, 48000.0, 30.0);
    REQUIRE (expected.size() > 80);

    for (int buffer : { 32, 64, 128, 256, 512, 1024, 7, 333 })
    {
        INFO ("buffer " << buffer);
        const auto clicks = renderClicks (172.0, 48000.0, buffer, 30.0);
        REQUIRE (clicks == expected);
    }
}

TEST_CASE ("T2: first click at sample 0, accent on beat 1, stop silences, replay restarts", "[metronome]")
{
    const auto r = render ({ 172.0, 48000.0, 128, 3.0 }, true);
    REQUIRE_FALSE (r.onsets.empty());
    REQUIRE (r.onsets.front() == 0);

    const int clickLength = 960; // 20 ms @ 48 kHz
    for (std::size_t k = 0; k < r.onsets.size(); ++k)
    {
        const int zc = zeroCrossings (r.output, r.onsets[k], clickLength);
        INFO ("beat " << k << " crossings " << zc);
        if (k % kBeatsPerBar == 0)
            REQUIRE (zc > 55);   // 1760 Hz: ~70 crossings in 20 ms
        else
            REQUIRE (zc < 45);   // 880 Hz: ~35 crossings
    }

    // stop: no clicks; play again: click at the first sample after Play
    EngineCore core;
    core.prepare (48000.0, 128, 0, 2);
    core.params().monitorEnabled = false;
    core.params().metronomeGain = 1.0f;
    std::vector<float> l (128), rr (128);
    float* out[2] = { l.data(), rr.data() };

    core.commands().push ({ CommandType::Play, 0.f, 0 });
    for (int i = 0; i < 100; ++i)
        core.process (nullptr, 0, out, 2, 128);

    core.commands().push ({ CommandType::Stop, 0.f, 0 });
    for (int i = 0; i < 400; ++i)
    {
        core.process (nullptr, 0, out, 2, 128);
        REQUIRE (std::all_of (l.begin(), l.end(), [] (float v) { return v == 0.f; }));
    }

    core.commands().push ({ CommandType::Play, 0.f, 0 });
    core.process (nullptr, 0, out, 2, 128);
    REQUIRE (l[0] != 0.f);
}

TEST_CASE ("T3: BPM change keeps the grid continuous", "[metronome]")
{
    struct Case { double from, to; };
    for (auto c : { Case { 172.0, 90.0 }, Case { 90.0, 300.0 } })
    {
        INFO ("from " << c.from << " to " << c.to);
        const double sr = 48000.0;
        const int buffer = 128;
        const int changeBlock = 1500; // 4 s in
        const auto r = render ({ c.from, sr, buffer, 20.0, changeBlock, c.to });

        const double spbFrom = sr * 60.0 / c.from;
        const double spbTo = sr * 60.0 / c.to;
        const double lo = std::min (spbFrom, spbTo) - 1.0;
        const double hi = std::max (spbFrom, spbTo) + 1.0;

        for (std::size_t i = 1; i < r.onsets.size(); ++i)
        {
            const auto interval = static_cast<double> (r.onsets[i] - r.onsets[i - 1]);
            REQUIRE (interval >= lo);
            REQUIRE (interval <= hi);
        }

        const int64_t changeSample = static_cast<int64_t> (changeBlock) * buffer;
        for (std::size_t i = 1; i < r.onsets.size(); ++i)
        {
            if (r.onsets[i - 1] >= changeSample)
            {
                const auto interval = static_cast<double> (r.onsets[i] - r.onsets[i - 1]);
                REQUIRE (std::fabs (interval - spbTo) <= 1.0);
            }
        }

        // analytic count: beats elapsed at the end = beats at change + rest at the new tempo
        const double beatsAtChange = static_cast<double> (changeSample) / spbFrom;
        const double totalSamples = 20.0 * sr;
        const double beatsAtEnd = beatsAtChange + (totalSamples - static_cast<double> (changeSample)) / spbTo;
        const auto expected = static_cast<long> (std::ceil (beatsAtEnd));
        REQUIRE (std::labs (static_cast<long> (r.onsets.size()) - expected) <= 1);
    }
}

TEST_CASE ("T4: sample rates agree in time, prepare resets state", "[metronome]")
{
    const auto a = renderClicks (172.0, 44100.0, 128, 10.0);
    const auto b = renderClicks (172.0, 48000.0, 128, 10.0);
    const auto c = renderClicks (172.0, 96000.0, 128, 10.0);
    REQUIRE (a.size() == b.size());
    REQUIRE (b.size() == c.size());

    const double tolerance = 1.0 / 44100.0 + 1e-12;
    for (std::size_t k = 0; k < a.size(); ++k)
    {
        const double ta = static_cast<double> (a[k]) / 44100.0;
        const double tb = static_cast<double> (b[k]) / 48000.0;
        const double tc = static_cast<double> (c[k]) / 96000.0;
        REQUIRE (std::fabs (ta - tb) <= tolerance);
        REQUIRE (std::fabs (tb - tc) <= tolerance);
    }

    EngineCore core;
    core.prepare (48000.0, 128, 0, 2);
    std::vector<float> l (128), r (128);
    float* out[2] = { l.data(), r.data() };
    core.commands().push ({ CommandType::Play, 0.f, 0 });
    for (int i = 0; i < 50; ++i)
        core.process (nullptr, 0, out, 2, 128);
    REQUIRE (core.transportSnapshot().playing);

    core.prepare (96000.0, 128, 0, 2);
    const auto s = core.transportSnapshot();
    REQUIRE_FALSE (s.playing);
    REQUIRE (s.positionSamples == 0);
}

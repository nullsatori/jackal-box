#include "RigHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace jackal;
using namespace jackal::test;

namespace
{
    double spbAt (double bpm, double sr = 48000.0) { return sr * 60.0 / bpm; }

    std::vector<int64_t> renderLane (const Pattern& p, double bpm, int buffer, double seconds, int track = 0)
    {
        OfflineRig rig (buffer);
        rig.setSample (0, makeImpulse());
        rig.setTrack (track, 0);
        rig.setPattern (0, p);
        rig.bpm (bpm);
        rig.play();
        rig.renderSeconds (seconds);
        return onsets (rig.left);
    }
} // namespace

TEST_CASE ("S1: straight 16ths are sample exact for any buffer size", "[seq]")
{
    auto p = laneAll (0);
    const double spb = spbAt (172.0);
    const double seconds = 6.0;

    std::vector<int64_t> expected;
    for (int64_t k = 0;; ++k)
    {
        const auto at = static_cast<int64_t> (std::llround (static_cast<double> (k) * spb / 4.0));
        if (at >= static_cast<int64_t> (seconds * 48000.0))
            break;
        expected.push_back (at);
    }

    for (int buffer : { 32, 64, 128, 256, 512, 1024, 7, 333 })
    {
        INFO ("buffer " << buffer);
        REQUIRE (renderLane (*p, 172.0, buffer, seconds) == expected);
    }
}

TEST_CASE ("S2: swing shifts odd steps only", "[seq]")
{
    auto straight = laneAll (0);
    auto swung = laneAll (0);
    swung->swing = 66;
    const double spb = spbAt (172.0);

    const auto a = renderLane (*straight, 172.0, 128, 4.0);
    const auto b = renderLane (*swung, 172.0, 128, 4.0);
    REQUIRE (a.size() == b.size());

    const auto shift = static_cast<int64_t> (std::llround (0.32 * spb / 4.0));
    for (std::size_t k = 0; k < a.size(); ++k)
    {
        if (k % 2 == 0)
            REQUIRE (a[k] == b[k]);
        else
            REQUIRE (std::llabs (b[k] - (a[k] + shift)) <= 1);
    }

    auto fifty = laneAll (0);
    fifty->swing = 50;
    REQUIRE (renderLane (*fifty, 172.0, 128, 4.0) == a);

    auto trip = laneAll (0, 16, LaneSpeed::Triplet);
    auto tripSwing = laneAll (0, 16, LaneSpeed::Triplet);
    tripSwing->swing = 75;
    REQUIRE (renderLane (*trip, 172.0, 128, 3.0) == renderLane (*tripSwing, 172.0, 128, 3.0));
}

TEST_CASE ("S3: lane length and speed", "[seq]")
{
    const double spb = spbAt (172.0);

    auto p = std::make_unique<Pattern>();
    p->lanes[0].length = 7;
    p->lanes[0].steps[0].active = true;
    const auto on = renderLane (*p, 172.0, 128, 4.0);
    REQUIRE (on.size() >= 3);
    for (std::size_t i = 0; i < on.size(); ++i)
        REQUIRE (on[i] == std::llround (static_cast<double> (7 * i) * spb / 4.0));

    struct Case { LaneSpeed speed; double interval; };
    for (auto c : { Case { LaneSpeed::Double, spb / 8.0 }, Case { LaneSpeed::Half, spb / 2.0 }, Case { LaneSpeed::Triplet, spb / 6.0 } })
    {
        auto lp = laneAll (0, 16, c.speed);
        const auto o = renderLane (*lp, 172.0, 128, 3.0);
        REQUIRE (o.size() > 4);
        for (std::size_t i = 0; i < o.size(); ++i)
            REQUIRE (o[i] == std::llround (static_cast<double> (i) * c.interval));
    }

    // two lanes of different length do not disturb each other
    auto two = std::make_unique<Pattern>();
    two->lanes[0].steps[0].active = true;           // length 16 -> every 4 beats
    two->lanes[1].length = 7;
    two->lanes[1].steps[0].active = true;           // every 7 steps
    OfflineRig rig (128);
    rig.setSample (0, makeImpulse());
    rig.setTrack (0, 0);
    rig.setTrack (1, 0);
    rig.param (0, TrackParam::Pan, -1.f);
    rig.param (1, TrackParam::Pan, 1.f);
    rig.setPattern (0, *two);
    rig.bpm (172.0);
    rig.play();
    rig.renderSeconds (6.0);
    const auto l = onsets (rig.left), r = onsets (rig.right);
    for (std::size_t i = 0; i < l.size(); ++i)
        REQUIRE (l[i] == std::llround (static_cast<double> (16 * i) * spb / 4.0));
    for (std::size_t i = 0; i < r.size(); ++i)
        REQUIRE (r[i] == std::llround (static_cast<double> (7 * i) * spb / 4.0));
}

TEST_CASE ("S4: tempo change mid-play: no lost or doubled steps", "[seq]")
{
    auto p = laneAll (0);
    for (int buffer : { 64, 128, 333 })
    {
        OfflineRig rig (buffer);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setPattern (0, *p);
        rig.bpm (172.0);
        rig.play();
        const int changeBlock = 300;
        int block = 0;
        rig.renderUntil (20 * 48000, [&] (int64_t) { if (block++ == changeBlock) rig.bpm (90.0); });

        const auto on = onsets (rig.left);
        const double t0 = static_cast<double> (changeBlock * buffer);
        const double beatsAtChange = t0 / spbAt (172.0);
        const double totalBeats = beatsAtChange + (20.0 * 48000.0 - t0) / spbAt (90.0);
        const auto expectedCount = static_cast<int64_t> (std::floor (totalBeats * 4.0)) + 1;
        REQUIRE (std::llabs (static_cast<int64_t> (on.size()) - expectedCount) <= 1);

        for (std::size_t i = 1; i < on.size(); ++i)
        {
            const auto d = on[i] - on[i - 1];
            if (on[i - 1] > static_cast<int64_t> (t0) + 1)
                REQUIRE (std::llabs (d - std::llround (spbAt (90.0) / 4.0)) <= 1);
        }
    }
}

TEST_CASE ("S5: pattern switch lands on the bar line", "[seq]")
{
    auto a = laneAll (0);
    auto b = laneAll (1);
    const double spb = spbAt (172.0);
    const auto bar = static_cast<int64_t> (std::llround (4.0 * spb));

    std::vector<int64_t> lastResultL, lastResultR;
    for (int buffer : { 64, 1024 })
    {
        OfflineRig rig (buffer);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setTrack (1, 0);
        rig.param (0, TrackParam::Pan, -1.f);
        rig.param (1, TrackParam::Pan, 1.f);
        rig.setPattern (0, *a);
        rig.setPattern (1, *b);
        rig.bpm (172.0);
        rig.play();

        const auto queueAt = static_cast<int64_t> (1.3 * spb);
        rig.renderUntil (queueAt);
        rig.cmd (CommandType::QueuePattern, 1);
        rig.renderUntil (bar * 2);

        const auto l = onsets (rig.left), r = onsets (rig.right);
        REQUIRE (! l.empty());
        REQUIRE (! r.empty());
        REQUIRE (l.back() <= bar - 1);
        REQUIRE (r.front() == bar);
        REQUIRE (r[1] == std::llround (4.0 * spb + spb / 4.0));   // B starts from its slot 0

        if (! lastResultL.empty())
        {
            REQUIRE (l == lastResultL);
            REQUIRE (r == lastResultR);
        }
        lastResultL = l;
        lastResultR = r;
    }

    // stopped: switch at once
    OfflineRig rig (128);
    rig.setPattern (0, *a);
    rig.setPattern (1, *b);
    rig.cmd (CommandType::QueuePattern, 3);
    rig.renderUntil (128);
    REQUIRE (rig.core.sequencerSnapshot().currentPattern == 3);
    REQUIRE (rig.core.sequencerSnapshot().pendingPattern == -1);
}

TEST_CASE ("S6: mute and solo", "[seq]")
{
    auto p = laneAll (0);
    p->lanes[1] = p->lanes[0];

    {
        OfflineRig rig (128);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setPattern (0, *p);
        rig.core.params().trackMute[0] = true;
        rig.play();
        rig.renderSeconds (2.0);
        REQUIRE (onsets (rig.left).empty());
    }
    {
        OfflineRig rig (128);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setTrack (1, 0);
        rig.param (0, TrackParam::Pan, -1.f);
        rig.param (1, TrackParam::Pan, 1.f);
        rig.setPattern (0, *p);
        rig.core.params().trackSolo[1] = true;
        rig.play();
        rig.renderSeconds (2.0);
        REQUIRE (onsets (rig.left).empty());
        REQUIRE (! onsets (rig.right).empty());
    }
    {
        // a ringing tail is not cut by mute
        auto one = std::make_unique<Pattern>();
        one->lanes[0].steps[0].active = true;
        OfflineRig rig (128);
        rig.setSample (0, makeDc (20000, 0.5f));
        rig.setTrack (0, 0);
        rig.setPattern (0, *one);
        rig.play();
        rig.renderUntil (1280);
        rig.core.params().trackMute[0] = true;
        rig.renderUntil (12800);
        REQUIRE (rig.left[12000] != 0.f);
    }
}

TEST_CASE ("R1: ratchets subdivide the step, also across blocks", "[seq][ratchet]")
{
    const double spb = spbAt (172.0);
    const double stepSamples = spb / 4.0;

    for (int r : { 3, 4, 6 })
        for (int buffer : { 128, 7 })
        {
            auto p = std::make_unique<Pattern>();
            p->lanes[0].steps[0].active = true;
            p->lanes[0].steps[0].ratchet = static_cast<uint8_t> (r);
            const auto on = renderLane (*p, 172.0, buffer, 0.5);
            INFO ("r " << r << " buffer " << buffer);
            REQUIRE (on.size() == static_cast<std::size_t> (r));
            for (int j = 0; j < r; ++j)
                REQUIRE (on[static_cast<std::size_t> (j)] == std::llround (j * stepSamples / r));
        }
}

TEST_CASE ("R2: ratchet velocity and pitch ramps", "[seq][ratchet]")
{
    const double spb = spbAt (172.0);
    const double stepSamples = spb / 4.0;

    {
        auto p = std::make_unique<Pattern>();
        auto& s = p->lanes[0].steps[0];
        s.active = true;
        s.velocity = 127;
        s.ratchet = 4;
        s.ratchetVelRamp = -100;

        OfflineRig rig (128);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setPattern (0, *p);
        rig.play();
        rig.renderSeconds (0.5);

        const float a0 = rig.left[0];
        REQUIRE (a0 != 0.f);
        const float expected[4] { 1.f, 2.f / 3.f, 1.f / 3.f, 0.f };
        for (int j = 0; j < 4; ++j)
        {
            const auto at = static_cast<std::size_t> (std::llround (j * stepSamples / 4));
            REQUIRE (std::fabs (rig.left[at] / a0 - expected[j]) < 1e-3f);
        }
    }
    {
        auto p = std::make_unique<Pattern>();
        auto& s = p->lanes[0].steps[0];
        s.active = true;
        s.ratchet = 2;
        s.ratchetPitchRamp = 12;

        OfflineRig rig (128);
        rig.setSample (0, makeRamp (2000));
        rig.setTrack (0, 0);
        rig.setPattern (0, *p);
        rig.play();
        rig.renderSeconds (0.5);

        auto runLength = [&] (std::size_t from) {
            std::size_t n = 0;
            while (from + n < rig.left.size() && rig.left[from + n] != 0.f)
                ++n;
            return static_cast<int64_t> (n);
        };
        const auto second = static_cast<std::size_t> (std::llround (stepSamples / 2.0));
        const auto first = runLength (0);
        const auto last = runLength (second);
        REQUIRE (first == 2000);
        REQUIRE (std::llabs (last - first / 2) <= 2);
    }
}

TEST_CASE ("R3: ratchet 64 at 300 BPM and trigger overflow", "[seq][ratchet]")
{
    std::vector<int64_t> reference;
    for (int buffer : { 4096, 1024, 32 })
    {
        auto p = std::make_unique<Pattern>();
        p->lanes[0].steps[0].active = true;
        p->lanes[0].steps[0].ratchet = 64;

        OfflineRig rig (buffer);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setPattern (0, *p);
        rig.bpm (300.0);
        rig.play();
        rig.renderUntil (2400 + 64);

        for (float v : rig.left)
            REQUIRE (std::isfinite (v));
        auto on = onsets (rig.left, 8);
        on.erase (std::remove_if (on.begin(), on.end(), [] (int64_t x) { return x >= 2400; }), on.end());
        REQUIRE (on.size() == 64);
        REQUIRE (rig.core.sequencerSnapshot().droppedCommands == 0);
        if (! reference.empty())
            REQUIRE (on == reference);
        reference = on;
    }

    OfflineRig rig (256);
    rig.setSample (0, makeImpulse());
    rig.setTrack (0, 0);
    rig.renderUntil (256);
    for (int i = 0; i < 200; ++i)
        rig.cmd (CommandType::TriggerTrack, 0, 100.f);
    rig.renderUntil (512);
    REQUIRE (rig.core.sequencerSnapshot().droppedCommands == 72);
}

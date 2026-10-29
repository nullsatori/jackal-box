#include "RigHelpers.h"

#include "Engine/StepHash.h"

#include <catch2/catch_test_macros.hpp>

using namespace jackal;
using namespace jackal::test;

namespace
{
    std::vector<int64_t> renderWith (const Pattern& p, uint32_t seed, int buffer, double seconds, bool fill = false)
    {
        OfflineRig rig (buffer);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setPattern (0, p);
        rig.cmd (CommandType::SetSeed, static_cast<int32_t> (seed));
        rig.core.params().fillHeld = fill;
        rig.play();
        rig.renderSeconds (seconds);
        return onsets (rig.left);
    }

    std::unique_ptr<Pattern> withProbability (int prob)
    {
        auto p = laneAll (0);
        for (auto& s : p->lanes[0].steps)
            s.probability = static_cast<uint8_t> (prob);
        return p;
    }
} // namespace

TEST_CASE ("P1: probability is deterministic and fair", "[prob]")
{
    auto p = withProbability (50);
    const double bars16 = 16.0 * 4.0 * 60.0 / 172.0;

    const auto a = renderWith (*p, 7, 128, bars16);
    REQUIRE (a == renderWith (*p, 7, 128, bars16));
    REQUIRE (renderWith (*p, 7, 64, bars16) == renderWith (*p, 7, 1024, bars16));
    REQUIRE (a != renderWith (*p, 8, 128, bars16));

    int played = 0, total = 0;
    for (int64_t k = 0; k < 16 * 1000; ++k, ++total)
        if (StepHash::unit (7, 0, 0, k) * 100.0 < 50.0)
            ++played;
    const double ratio = static_cast<double> (played) / total;
    REQUIRE (ratio >= 0.45);
    REQUIRE (ratio <= 0.55);

    REQUIRE (renderWith (*withProbability (0), 7, 128, 2.0).empty());
    REQUIRE (renderWith (*withProbability (100), 7, 128, 2.0).size() == renderWith (*laneAll (0), 7, 128, 2.0).size());
}

TEST_CASE ("P2: conditions follow lane cycles and fill", "[prob]")
{
    const double spb = 48000.0 * 60.0 / 172.0;

    for (int len : { 16, 7 })
    {
        auto p = std::make_unique<Pattern>();
        p->lanes[0].length = static_cast<uint8_t> (len);
        p->lanes[0].steps[0].active = true;
        p->lanes[0].steps[0].condition = StepCondition::Cycle1of2;
        const auto on = renderWith (*p, 1, 128, 6.0);
        REQUIRE (! on.empty());
        for (std::size_t i = 0; i < on.size(); ++i)
            REQUIRE (on[i] == std::llround (static_cast<double> (2 * len * static_cast<int> (i)) * spb / 4.0));

        p->lanes[0].steps[0].condition = StepCondition::Cycle2of2;
        const auto on2 = renderWith (*p, 1, 128, 6.0);
        REQUIRE (! on2.empty());
        for (std::size_t i = 0; i < on2.size(); ++i)
            REQUIRE (on2[i] == std::llround (static_cast<double> (len * (2 * static_cast<int> (i) + 1)) * spb / 4.0));
    }

    // FirstCycle: once after Play, again after a pattern switch
    auto first = std::make_unique<Pattern>();
    first->lanes[0].steps[0].active = true;
    first->lanes[0].steps[0].condition = StepCondition::FirstCycle;
    {
        OfflineRig rig (128);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setPattern (0, *first);
        rig.setPattern (1, *first);
        rig.play();
        rig.renderSeconds (4.0);
        REQUIRE (onsets (rig.left).size() == 1);
        rig.cmd (CommandType::QueuePattern, 1);
        rig.renderSeconds (4.0);
        REQUIRE (onsets (rig.left).size() == 2);
    }

    auto fill = laneAll (0);
    for (auto& s : fill->lanes[0].steps)
        s.condition = StepCondition::Fill;
    REQUIRE (renderWith (*fill, 1, 128, 1.0, false).empty());
    REQUIRE (! renderWith (*fill, 1, 128, 1.0, true).empty());
    for (auto& s : fill->lanes[0].steps)
        s.condition = StepCondition::NotFill;
    REQUIRE (! renderWith (*fill, 1, 128, 1.0, false).empty());
    REQUIRE (renderWith (*fill, 1, 128, 1.0, true).empty());
}

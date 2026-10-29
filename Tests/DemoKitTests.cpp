#include "RigHelpers.h"

#include "Model/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>

using namespace jackal;
using namespace jackal::test;

TEST_CASE ("D1: every built-in sound is real, normalized and deterministic", "[demo]")
{
    const std::map<std::string, double> lengthsMs {
        { "kick", 350 }, { "snare", 220 }, { "clap", 183 }, { "hat_closed", 60 },
        { "hat_open", 320 }, { "rim", 70 }, { "tom", 280 }, { "break", 2.0 * 4.0 * 60.0 / 172.0 * 1000.0 } };

    for (auto name : builtinNames())
    {
        INFO (name);
        auto s = makeBuiltin (name, 48000.0);
        REQUIRE (s != nullptr);
        double sum = 0.0;
        float peak = 0.f;
        for (float v : s->data[0])
        {
            REQUIRE (std::isfinite (v));
            sum += static_cast<double> (v) * v;
            peak = std::max (peak, std::fabs (v));
        }
        REQUIRE (std::sqrt (sum / static_cast<double> (s->length)) > 0.01);
        REQUIRE (peak >= 0.89f);
        REQUIRE (peak <= 0.91f);
        const double ms = static_cast<double> (s->length) / 48.0;
        REQUIRE (std::fabs (ms - lengthsMs.at (std::string (name))) <= 1.0);

        auto again = makeBuiltin (name, 48000.0);
        REQUIRE (again->data[0] == s->data[0]);
    }

    auto br = makeBuiltin ("break", 48000.0);
    REQUIRE (br->sourceBars == 2.0);
    REQUIRE (br->numSlices() == 32);
    REQUIRE (makeBuiltin ("nope", 48000.0) == nullptr);
}

TEST_CASE ("D2: demo project", "[demo]")
{
    auto p = makeDemoProject (48000.0);
    for (int t = 0; t < kNumTracks; ++t)
        REQUIRE (p->tracks[static_cast<std::size_t> (t)].sampleSlot == t);

    auto activeSteps = [] (const Lane& l) { int n = 0; for (const auto& s : l.steps) n += s.active ? 1 : 0; return n; };
    for (int t : { 0, 1, 3, 4, 7 })
        REQUIRE (activeSteps (p->patterns[0].lanes[static_cast<std::size_t> (t)]) > 0);
    REQUIRE (! (p->patterns[1] == p->patterns[0]));
    for (int i = 2; i < kNumPatterns; ++i)
        REQUIRE (p->patterns[static_cast<std::size_t> (i)] == Pattern {});

    auto r = fromJson (toJson (*p));
    REQUIRE (r.ok);
    REQUIRE (*r.project == *p);

    auto render = [] (Project& proj) {
        OfflineRig rig (256);
        rig.loadProject (proj);
        rig.play();
        rig.renderSeconds (8.0 * 4.0 * 60.0 / 172.0);
        return rig.left;
    };
    const auto a = render (*p);
    REQUIRE (a == render (*p));
    float peak = 0.f;
    for (float v : a)
    {
        REQUIRE (std::isfinite (v));
        peak = std::max (peak, std::fabs (v));
    }
    REQUIRE (peak > 0.01f);
    REQUIRE (peak < 4.0f);
}

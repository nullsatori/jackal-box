#include "Model/DemoKit.h"
#include "Model/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace jackal;

namespace
{
    std::unique_ptr<Project> everything()
    {
        auto p = std::make_unique<Project>();
        p->name = "Jungle Tool";
        p->bpm = 166.5;
        p->seed = 0xDEADBEEF;
        p->masterGainDb = -3.25f;
        p->slots[0] = { "builtin:kick", "", SliceMode::Off, 0.0, {}, true };
        p->slots[3] = { "file", "samples/riff.wav", SliceMode::Transient, 1.0, { 0, 4410, 9200 }, true };
        p->slots[9] = { "file", "samples/amen.wav", SliceMode::Grid32, 2.0, {}, true };

        for (int t = 0; t < kNumTracks; ++t)
        {
            auto& tr = p->tracks[static_cast<std::size_t> (t)];
            tr.name = "T" + std::to_string (t);
            tr.sampleSlot = static_cast<int8_t> (t);
            tr.chokeGroup = static_cast<uint8_t> (t % 5);
            tr.fitToTempo = t % 2 == 0;
            tr.mute = t == 3;
            tr.solo = t == 4;
            tr.params = { -3.5f, 0.25f, -7.f, 120.f, 900.f, 0.1f, 0.9f, 0.f };
        }

        const LaneSpeed speeds[4] { LaneSpeed::Half, LaneSpeed::Normal, LaneSpeed::Double, LaneSpeed::Triplet };
        const StepCondition conds[6] { StepCondition::Always, StepCondition::Fill, StepCondition::NotFill,
                                       StepCondition::FirstCycle, StepCondition::Cycle1of2, StepCondition::Cycle2of2 };
        for (int pi = 0; pi < kNumPatterns; ++pi)
        {
            auto& pat = p->patterns[static_cast<std::size_t> (pi)];
            pat.bars = static_cast<uint8_t> (1 + pi % 4);
            pat.swing = static_cast<uint8_t> (50 + pi * 3);
            for (int t = 0; t < kNumTracks; ++t)
            {
                auto& lane = pat.lanes[static_cast<std::size_t> (t)];
                lane.length = static_cast<uint8_t> (7 + t * 5);
                lane.speed = speeds[t % 4];
                for (int s = 0; s < 6; ++s)
                {
                    auto& st = lane.steps[static_cast<std::size_t> (s * 3)];
                    st.active = s != 5;   // the last one is an erased step that keeps its labels
                    st.velocity = static_cast<uint8_t> (20 + s * 10);
                    st.ratchet = static_cast<uint8_t> (1 + s);
                    st.ratchetVelRamp = static_cast<int8_t> (-50 + s * 10);
                    st.ratchetPitchRamp = static_cast<int8_t> (s * 2);
                    st.pitch = static_cast<int8_t> (-s);
                    st.slice = static_cast<int8_t> (s - 1);
                    st.reverse = s % 2 == 1;
                    st.probability = static_cast<uint8_t> (40 + s * 10);
                    st.condition = conds[s];
                    st.nudge = static_cast<int8_t> (s * 4 - 10);
                }
                auto& locked = lane.steps[1];
                locked.active = true;
                locked.plocks = { PLock { 2, 12.f }, PLock { 3, 80.f }, PLock { 4, 1500.f }, PLock { 1, -0.5f } };
            }
            for (int c = 0; c < pat.bars * 16; ++c)
                pat.glitch.pad[static_cast<std::size_t> (c)] = static_cast<uint8_t> (c % 9);
        }
        p->ui = { 5, 2, 1, 0.75f };
        return p;
    }
} // namespace

TEST_CASE ("J1: project JSON round trip", "[json]")
{
    auto p = everything();
    const auto text = toJson (*p);
    auto r = fromJson (text);
    REQUIRE (r.ok);
    REQUIRE (*r.project == *p);
    REQUIRE (toJson (*r.project) == text);
}

TEST_CASE ("J2: tolerant reading, strict format checks", "[json]")
{
    auto ok = fromJson (R"({"format":"jackal-project","version":2,"what":1,"bpm":140,
        "patterns":[{"swing":10,"lanes":[{"steps":[{"i":3,"v":300}]}]}]})");
    REQUIRE (ok.ok);
    REQUIRE (ok.project->bpm == 140.0);
    REQUIRE (ok.project->name == "Untitled");
    REQUIRE (ok.project->patterns[0].swing == 50);
    REQUIRE (ok.project->patterns[0].lanes[0].steps[3].active);
    REQUIRE (ok.project->patterns[0].lanes[0].steps[3].velocity == 127);
    REQUIRE (ok.project->patterns[0].lanes[0].length == 16);

    auto v3 = fromJson (R"({"format":"jackal-project","version":3})");
    REQUIRE (! v3.ok);
    REQUIRE (! v3.error.empty());
    REQUIRE (! fromJson (R"({"format":"other","version":2})").ok);
    REQUIRE (! fromJson ("{ broken json").ok);
    REQUIRE (! fromJson ("").ok);
}

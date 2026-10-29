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

    // stage 3: version 3 is current, version 4 is the first unknown one
    REQUIRE (fromJson (R"({"format":"jackal-project","version":3})").ok);
    auto v4 = fromJson (R"({"format":"jackal-project","version":4})");
    REQUIRE (! v4.ok);
    REQUIRE (! v4.error.empty());
    REQUIRE (! fromJson (R"({"format":"other","version":2})").ok);
    REQUIRE (! fromJson ("{ broken json").ok);
    REQUIRE (! fromJson ("").ok);
}

TEST_CASE ("J3: version 2 migrates to version 3", "[json]")
{
    const char* v2 = R"({"format":"jackal-project","version":2,"name":"Old","bpm":170,"seed":7,"masterGainDb":-6,
        "tracks":[{"name":"KICK","slot":0,"params":{"volume":-2,"shakal":0.7}},{"name":"BREAK","slot":7}],
        "patterns":[{"bars":1,"swing":55,"lanes":[{"length":16,"speed":"x1","steps":[{"i":0,"v":120}]}]}]})";
    auto r = fromJson (v2);
    REQUIRE (r.ok);
    REQUIRE (r.migratedFrom == 2);
    REQUIRE (r.project->masterShakal == 0.f);
    REQUIRE (r.project->masterFx == FxSettings {});
    for (const auto& t : r.project->tracks)
    {
        REQUIRE (t.fx == FxSettings {});
        REQUIRE (t.params[static_cast<std::size_t> (TrackParam::Shakal)] == 0.f);
    }
    REQUIRE (r.project->tracks[0].params[0] == -2.f);

    const auto text = toJson (*r.project);
    REQUIRE (text.find ("\"version\": 3") != std::string::npos);
    auto again = fromJson (text);
    REQUIRE (again.ok);
    REQUIRE (again.migratedFrom == 0);
    REQUIRE (*again.project == *r.project);
}

TEST_CASE ("J4: version 3 SHAKAL settings roundtrip", "[json]")
{
    auto p = std::make_unique<Project>();
    auto fill = [] (FxSettings& f, int k) {
        f.preset = static_cast<int8_t> (k % 3);
        f.bypass = k == 4;
        auto set = [&f] (FxParam q, float v) { f.params[static_cast<std::size_t> (q)] = v; };
        set (FxParam::DriveOn, 1.f);       set (FxParam::DriveDb, 3.5f + static_cast<float> (k));
        set (FxParam::DriveMode, static_cast<float> (k % 2));
        set (FxParam::Sp12On, k % 2 == 0 ? 1.f : 0.f);  set (FxParam::Sp12FilterHz, 4000.f + 100.f * static_cast<float> (k));
        set (FxParam::DecimOn, 1.f);       set (FxParam::DecimRateHz, 9000.f - 250.f * static_cast<float> (k));
        set (FxParam::CrushOn, 1.f);       set (FxParam::CrushBits, 6.25f);
        set (FxParam::CrushMulaw, static_cast<float> ((k + 1) % 2));
        set (FxParam::FlipOn, 1.f);        set (FxParam::FlipProb, 0.125f);
        set (FxParam::FlipBit, static_cast<float> (k + 3));
        set (FxParam::FlipMode, static_cast<float> ((k / 2) % 2));
        set (FxParam::GlitchOn, 1.f);      set (FxParam::GlitchProb, 0.5f);  set (FxParam::GlitchLenMs, 40.f + static_cast<float> (k));
    };
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& tr = p->tracks[static_cast<std::size_t> (t)];
        fill (tr.fx, t);
        tr.params[static_cast<std::size_t> (TrackParam::Shakal)] = 0.125f * static_cast<float> (t);
    }
    fill (p->masterFx, 8);
    p->masterShakal = 0.375f;
    p->ui.trackPanelTab = 3;
    p->patterns[0].lanes[7].steps[6].active = true;
    p->patterns[0].lanes[7].steps[6].plocks[0] = { static_cast<uint8_t> (TrackParam::Shakal), 1.f };

    const auto text = toJson (*p);
    auto r = fromJson (text);
    REQUIRE (r.ok);
    REQUIRE (*r.project == *p);
    REQUIRE (toJson (*r.project) == text);

    auto odd = fromJson (R"({"format":"jackal-project","version":3,
        "tracks":[{"fx":{"preset":"tape","params":{"crushBits":40,"driveMode":"fold"}}}]})");
    REQUIRE (odd.ok);
    REQUIRE (odd.project->tracks[0].fx.preset == 0);
    REQUIRE (odd.project->tracks[0].fx.params[static_cast<std::size_t> (FxParam::CrushBits)] == 16.f);
    REQUIRE (odd.project->tracks[0].fx.params[static_cast<std::size_t> (FxParam::DriveMode)] == 0.f);

    REQUIRE (! fromJson (R"({"format":"jackal-project","version":4})").ok);

    auto bare = fromJson (R"({"format":"jackal-project","version":3,"tracks":[{"name":"X"}]})");
    REQUIRE (bare.ok);
    REQUIRE (bare.project->tracks[0].fx == FxSettings {});
    REQUIRE (bare.project->masterFx == FxSettings {});
}

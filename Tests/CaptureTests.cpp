// Stage 5: retro capture of live hits (H1-H2). Offline, nothing is played.

#include "RigHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace jackal;
using namespace jackal::test;

namespace
{
    constexpr int64_t kBar = 96000, kStep = 6000;   // 120 BPM, 48 kHz

    std::vector<EngineEvent> captured (const OfflineRig& rig, std::size_t from = 0)
    {
        std::vector<EngineEvent> out;
        for (std::size_t i = from; i < rig.events.size(); ++i)
            if (rig.events[i].type == EngineEventType::StepRecorded || rig.events[i].type == EngineEventType::CaptureDone)
                out.push_back (rig.events[i]);
        return out;
    }

    std::vector<std::pair<int, int>> slots (const std::vector<EngineEvent>& ev)
    {
        std::vector<std::pair<int, int>> out;
        for (const auto& e : ev)
            out.emplace_back (e.type == EngineEventType::CaptureDone ? -1 : e.track, e.step);
        return out;
    }

    void hitAt (OfflineRig& rig, int64_t at, int track = 2, int vel = 100)
    {
        rig.renderUntil (at);
        rig.cmd (CommandType::TriggerTrack, track, static_cast<float> (vel));
    }
} // namespace

TEST_CASE ("H1: capture quantizes the last bars of live hits into steps", "[capture]")
{
    auto setup = [] (OfflineRig& rig, const Pattern& p) {
        rig.setPattern (0, p);
        rig.bpm (120.0);
        rig.play();
    };

    {
        OfflineRig rig (600);
        setup (rig, Pattern {});
        hitAt (rig, 30000);                              // bar 0: outside the 2-bar window
        hitAt (rig, kBar + 31 * kStep / 10);             // slot 3 + 10 %
        hitAt (rig, kBar + 96 * kStep / 10);             // slot 9 + 60 % -> 10
        hitAt (rig, 2 * kBar + kStep);                   // bar 2, slot 1
        rig.renderUntil (3 * kBar);
        const std::size_t mark = rig.events.size();
        rig.capture (2);
        rig.renderUntil (3 * kBar + 600);
        REQUIRE (slots (captured (rig, mark)) == std::vector<std::pair<int, int>> { { 2, 3 }, { 2, 10 }, { 2, 1 }, { -1, 0 } });

        const std::size_t mark1 = rig.events.size();
        rig.capture (1);
        rig.renderUntil (3 * kBar + 1200);
        REQUIRE (slots (captured (rig, mark1)) == std::vector<std::pair<int, int>> { { 2, 1 }, { -1, 0 } });
    }

    SECTION ("double speed, length 7, hits before the pattern start")
    {
        Pattern p;
        p.lanes[2].speed = LaneSpeed::Double;
        p.lanes[3].length = 7;
        OfflineRig rig (600);
        setup (rig, p);
        hitAt (rig, 5 * 3000 + 600, 2);    // 1/32 slot 5 (+20 %)
        hitAt (rig, 9 * kStep, 3);         // slot 9 of a 7-step lane -> 2
        rig.renderUntil (kBar);
        const std::size_t mark = rig.events.size();
        rig.capture (1);
        rig.renderUntil (kBar + 600);
        REQUIRE (slots (captured (rig, mark)) == std::vector<std::pair<int, int>> { { 2, 5 }, { 3, 2 }, { -1, 0 } });
    }

    SECTION ("k < 0: hits from before the current pattern wrap around")
    {
        Pattern a, b;
        OfflineRig rig (600);
        rig.setPattern (1, b);
        setup (rig, a);
        hitAt (rig, 2 * kStep);            // pattern A, beat 0.5
        rig.cmd (CommandType::QueuePattern, 1);
        rig.renderUntil (kBar + 1200);     // B started at bar 1
        REQUIRE (rig.snapshot().currentPattern == 1);
        const std::size_t mark = rig.events.size();
        rig.capture (2);
        rig.renderUntil (kBar + 1800);
        // kf = (0.5 - 4) / 0.25 = -14 -> slot 2
        REQUIRE (slots (captured (rig, mark)) == std::vector<std::pair<int, int>> { { 2, 2 }, { -1, 0 } });
    }

    SECTION ("stopped: nothing; Stop clears the history")
    {
        OfflineRig rig (600);
        setup (rig, Pattern {});
        hitAt (rig, kStep);
        rig.renderUntil (kBar);
        rig.stop();
        rig.renderUntil (kBar + 600);
        std::size_t mark = rig.events.size();
        rig.capture (2);
        rig.renderUntil (kBar + 1200);
        REQUIRE (captured (rig, mark).empty());

        rig.play();
        rig.renderUntil (kBar + 1800);
        mark = rig.events.size();
        rig.capture (4);
        rig.renderUntil (kBar + 2400);
        REQUIRE (slots (captured (rig, mark)) == std::vector<std::pair<int, int>> { { -1, 0 } });
    }
}

TEST_CASE ("H2: capture overflow and the 4096-hit ring", "[capture]")
{
    OfflineRig rig (64);
    rig.setPattern (0, Pattern {});
    rig.bpm (120.0);
    rig.play();
    // 5000 hits, two per block; hit i (1-based) is track i % 8, velocity i % 127 + 1
    for (int i = 1; i <= 5000; i += 2)
    {
        rig.cmd (CommandType::TriggerTrack, i % 8, static_cast<float> (i % 127 + 1));
        rig.cmd (CommandType::TriggerTrack, (i + 1) % 8, static_cast<float> ((i + 1) % 127 + 1));
        rig.renderUntil (rig.position() + 64);
    }
    const uint32_t droppedBefore = rig.snapshot().droppedCommands;
    const std::size_t mark = rig.events.size();
    rig.capture (4);
    rig.renderUntil (rig.position() + 64);
    const auto ev = captured (rig, mark);

    REQUIRE (ev.size() <= 1023);
    REQUIRE (ev.size() >= 1000);
    REQUIRE (rig.snapshot().droppedCommands > droppedBefore);
    // the oldest kept hit is #905: hits #1..#904 were overwritten (see DEVIATIONS.md, stage 5, H2)
    REQUIRE (ev.front().type == EngineEventType::StepRecorded);
    REQUIRE (ev.front().track == 905 % 8);
    REQUIRE (ev.front().value == 905 % 127 + 1);
}

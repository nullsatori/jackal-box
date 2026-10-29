#include "RigHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace jackal;
using namespace jackal::test;

namespace
{
    std::vector<EngineEvent> recorded (const OfflineRig& rig)
    {
        std::vector<EngineEvent> out;
        for (const auto& e : rig.events)
            if (e.type == EngineEventType::StepRecorded)
                out.push_back (e);
        return out;
    }

    // Hit track 0 at `beats` after play with REC on; returns the recorded slots.
    std::vector<EngineEvent> hitAt (double beats, int length, LaneSpeed speed, bool rec, bool viaMidi = false)
    {
        auto p = std::make_unique<Pattern>();
        p->lanes[0].length = static_cast<uint8_t> (length);
        p->lanes[0].speed = speed;

        OfflineRig rig (64);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        rig.setPattern (0, *p);
        rig.cmd (CommandType::SetRecording, rec ? 1 : 0);
        rig.play();
        const auto at = static_cast<int64_t> (std::llround (beats * 48000.0 * 60.0 / 172.0));
        rig.renderUntil (at);
        if (viaMidi)
            rig.core.midi().push ({ 0x90, 36, 90 });   // C2 -> track 0
        else
            rig.cmd (CommandType::TriggerTrack, 0, 90.f);
        rig.renderUntil (at + 64);
        REQUIRE (! onsets (rig.left).empty());
        REQUIRE (onsets (rig.left).back() == at);   // heard at once
        return recorded (rig);
    }
} // namespace

TEST_CASE ("Q1: live hits are quantized into steps while recording", "[rec]")
{
    auto slot = [] (const std::vector<EngineEvent>& ev) { REQUIRE (ev.size() == 1); return static_cast<int> (ev[0].step); };

    REQUIRE (slot (hitAt (5.1 * 0.25, 16, LaneSpeed::Normal, true)) == 5);
    REQUIRE (slot (hitAt (5.6 * 0.25, 16, LaneSpeed::Normal, true)) == 6);
    REQUIRE (slot (hitAt (15.6 * 0.25, 16, LaneSpeed::Normal, true)) == 0);
    REQUIRE (slot (hitAt (6.6 * 0.25, 7, LaneSpeed::Normal, true)) == 0);
    REQUIRE (slot (hitAt (8.4 * 0.25, 7, LaneSpeed::Normal, true)) == 1);
    REQUIRE (slot (hitAt (5.1 * 0.125, 16, LaneSpeed::Double, true)) == 5);
    REQUIRE (hitAt (5.1 * 0.25, 16, LaneSpeed::Normal, true)[0].value == 90);

    REQUIRE (hitAt (5.1 * 0.25, 16, LaneSpeed::Normal, false).empty());
    REQUIRE (slot (hitAt (5.1 * 0.25, 16, LaneSpeed::Normal, true, true)) == 5);

    // NoteOff and CC do nothing
    OfflineRig rig (64);
    rig.setSample (0, makeImpulse());
    rig.setTrack (0, 0);
    rig.cmd (CommandType::SetRecording, 1);
    rig.play();
    rig.renderUntil (6400);
    rig.core.midi().push ({ 0x80, 36, 0 });
    rig.core.midi().push ({ 0x90, 36, 0 });
    rig.core.midi().push ({ 0xB0, 1, 64 });
    rig.renderUntil (6464);
    REQUIRE (onsets (rig.left).empty());
    REQUIRE (recorded (rig).empty());
}

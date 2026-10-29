// Stage 5: MIDI note map in the engine (MM3), addresses, scales, dispatcher and map JSON (MM1, MM2, MM4, MM5).
// Offline, nothing is played and no MIDI device is opened.

#include "RigHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace jackal;
using namespace jackal::test;

namespace
{
    constexpr int64_t kBeat = 24000;

    MidiNoteMap mm3Map()
    {
        MidiNoteMap m;
        m.channel = 2;
        m.notes[36] = { NoteAction::TrackHit, 7 };
        m.notes[37] = { NoteAction::Pad, 0 };
        m.notes[38] = { NoteAction::Fill, 0 };
        return m;
    }

    int count (const OfflineRig& rig, EngineEventType t, std::size_t from = 0)
    {
        int c = 0;
        for (std::size_t i = from; i < rig.events.size(); ++i)
            c += rig.events[i].type == t ? 1 : 0;
        return c;
    }
} // namespace

TEST_CASE ("MM3: the note map drives hits, pads and fill in the engine", "[midi]")
{
    SECTION ("hits, channel filter, unmapped notes")
    {
        OfflineRig rig (64);
        rig.setSample (0, makeImpulse());
        rig.setTrack (7, 0);
        rig.setTrack (4, 0);
        rig.noteMap (mm3Map());
        rig.cmd (CommandType::SetRecording, 1);
        rig.bpm (120.0);
        rig.play();
        rig.renderUntil (6400);
        std::size_t mark = rig.events.size();
        rig.midi (0x91, 36, 100);
        rig.renderUntil (6464);
        REQUIRE (onsets (rig.left) == std::vector<int64_t> { 6400 });
        REQUIRE (count (rig, EngineEventType::StepRecorded, mark) == 1);
        REQUIRE (rig.events.back().track == 7);

        rig.renderUntil (12800);
        mark = rig.events.size();
        rig.midi (0x90, 36, 100);   // channel 1: filtered
        rig.midi (0x91, 40, 100);   // 40 % 12 = 4, but not in the map
        rig.renderUntil (12864);
        REQUIRE (onsets (rig.left).size() == 1);
        REQUIRE (count (rig, EngineEventType::StepRecorded, mark) == 0);
    }

    SECTION ("pads")
    {
        OfflineRig rig (64);
        rig.noteMap (mm3Map());
        rig.cmd (CommandType::SetRecording, 1);
        rig.bpm (120.0);
        rig.play();
        rig.renderUntil (1000);
        rig.midi (0x91, 37, 100);
        rig.renderUntil (6000 + 64);
        REQUIRE (rig.snapshot().activePad == 0);
        rig.renderUntil (24000);
        REQUIRE (count (rig, EngineEventType::GlitchRecorded) > 0);
        rig.midi (0x81, 37, 0);
        rig.renderUntil (24064);
        REQUIRE (rig.snapshot().activePad == -1);
    }

    SECTION ("fill")
    {
        OfflineRig rig (64);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        Pattern p;
        p.lanes[0].steps[0].active = true;
        p.lanes[0].steps[0].condition = StepCondition::Fill;
        p.lanes[0].steps[8].active = true;
        p.lanes[0].steps[8].condition = StepCondition::NotFill;
        rig.setPattern (0, p);
        rig.noteMap (mm3Map());
        rig.bpm (120.0);
        rig.play();
        rig.midi (0x91, 38, 100);
        rig.renderUntil (3 * kBeat);
        rig.midi (0x91, 38, 0);
        rig.renderUntil (8 * kBeat - 1);
        REQUIRE (onsets (rig.left) == std::vector<int64_t> { 0, 6 * kBeat });
    }

    SECTION ("nullptr = the stage 2 map; swaps retire the old map once, never the default")
    {
        OfflineRig rig (64);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        const MidiNoteMap* first = rig.noteMap (mm3Map());
        rig.renderUntil (64);
        rig.noteMap (nullptr);
        rig.play();
        rig.renderUntil (6400);
        rig.midi (0x90, 36, 90);   // C2 -> track 0, as Q1
        rig.renderUntil (6464);
        REQUIRE (onsets (rig.left) == std::vector<int64_t> { 6400 });
        REQUIRE (rig.countRetired (first) == 1);

        rig.cmd (CommandType::SetNoteMap, 0, 0.f, &kDefaultNoteMap);
        rig.renderUntil (6528);
        rig.noteMap (mm3Map());
        rig.renderUntil (6592);
        REQUIRE (rig.countRetired (&kDefaultNoteMap) == 0);
        REQUIRE (rig.countRetired (first) == 1);
    }
}

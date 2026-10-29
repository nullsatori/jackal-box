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

// ---------------------------------------------------------------- model side

#include "Model/MidiDispatcher.h"
#include "Model/MidiMapJson.h"

#include <catch2/catch_approx.hpp>

#include <cmath>
#include <map>

TEST_CASE ("MM1: every defined ParamId has an address that maps back, scales follow the table", "[midi][model]")
{
    int defined = 0;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = static_cast<ParamId> (i);
        if (! isDefined (id))
            continue;
        ++defined;
        const auto a = address (id);
        INFO (a);
        const auto back = fromAddress (a);
        REQUIRE (back.has_value());
        REQUIRE (*back == id);
    }
    REQUIRE (defined == 2 + kNumTracks * kNumTrackParams + kNumChains * kNumFxParams + kNumGuitarParams + kNumChains * kSlotsPerChain);

    REQUIRE (! isDefined (kGlitchBase));
    REQUIRE (! isDefined (static_cast<ParamId> (kFxBase + kNumFxParams)));
    REQUIRE (! isDefined (static_cast<ParamId> (0x0600)));
    REQUIRE (! fromAddress ("track.8.volume"));
    REQUIRE (! fromAddress ("fx.9.crushBits"));
    REQUIRE (! fromAddress ("nope"));
    REQUIRE (address (fxParam (kMasterChain, FxParam::CrushBits)) == "fx.master.crushBits");
    REQUIRE (address (fxParam (kGuitarChain, FxParam::DecimRateHz)) == "fx.guitar.decimRateHz");
    REQUIRE (address (insertParam (kGuitarChain, 1, InsertParam::Bypass)) == "insert.guitar.1.bypass");
    REQUIRE (address (insertParam (kMasterChain, 1, InsertParam::Bypass)) == "insert.master.1.bypass");
    REQUIRE (address (masterParam (MasterParam::Gain)) == "master.gain");
    REQUIRE (address (masterParam (MasterParam::Shakal)) == "master.shakal");
    REQUIRE (address (trackParam (3, TrackParam::Shakal)) == "track.3.shakal");
    REQUIRE (address (fxParam (7, FxParam::CrushBits)) == "fx.7.crushBits");
    REQUIRE (address (guitarParam (GuitarParam::HpfHz)) == "guitar.hpfHz");

    using S = ParamScale;
    const std::map<TrackParam, S> track { { TrackParam::Volume, S::Linear }, { TrackParam::Pan, S::Linear }, { TrackParam::Pitch, S::Linear },
                                          { TrackParam::Start, S::Linear }, { TrackParam::End, S::Linear }, { TrackParam::Shakal, S::Linear },
                                          { TrackParam::Decay, S::Log }, { TrackParam::Tone, S::Log } };
    for (int t = 0; t < kNumTracks; ++t)
        for (const auto& [p, s] : track)
            REQUIRE (info (trackParam (t, p)).scale == s);
    REQUIRE (info (masterParam (MasterParam::Gain)).scale == S::Linear);
    REQUIRE (info (masterParam (MasterParam::Shakal)).scale == S::Linear);

    const std::map<FxParam, S> fx {
        { FxParam::Bypass, S::Toggle }, { FxParam::Preset, S::Choice }, { FxParam::DriveOn, S::Toggle }, { FxParam::DriveDb, S::Linear },
        { FxParam::DriveMode, S::Toggle }, { FxParam::Sp12On, S::Toggle }, { FxParam::Sp12FilterHz, S::Log }, { FxParam::DecimOn, S::Toggle },
        { FxParam::DecimRateHz, S::Log }, { FxParam::CrushOn, S::Toggle }, { FxParam::CrushBits, S::Linear }, { FxParam::CrushMulaw, S::Toggle },
        { FxParam::FlipOn, S::Toggle }, { FxParam::FlipProb, S::Linear }, { FxParam::FlipBit, S::Linear }, { FxParam::FlipMode, S::Toggle },
        { FxParam::GlitchOn, S::Toggle }, { FxParam::GlitchProb, S::Linear }, { FxParam::GlitchLenMs, S::Log } };
    REQUIRE (fx.size() == static_cast<std::size_t> (kNumFxParams));
    for (int c = 0; c < kNumChains; ++c)
        for (const auto& [p, s] : fx)
            REQUIRE (info (fxParam (c, p)).scale == s);

    REQUIRE (info (guitarParam (GuitarParam::Shakal)).scale == S::Linear);
    REQUIRE (info (guitarParam (GuitarParam::GateDb)).scale == S::Linear);
    REQUIRE (info (guitarParam (GuitarParam::LoopGainDb)).scale == S::Linear);
    REQUIRE (info (guitarParam (GuitarParam::HpfHz)).scale == S::Log);
    REQUIRE (info (guitarParam (GuitarParam::LoopTap)).scale == S::Choice);
    for (int c = 0; c < kNumChains; ++c)
        for (int sl = 0; sl < kSlotsPerChain; ++sl)
            REQUIRE (info (insertParam (c, sl, InsertParam::Bypass)).scale == S::Toggle);
}

TEST_CASE ("MM2: CC scales", "[midi][model]")
{
    const auto& vol = info (trackParam (0, TrackParam::Volume));
    REQUIRE (ccToValue (vol, 0) == -60.f);
    REQUIRE (ccToValue (vol, 127) == 12.f);
    REQUIRE (ccToValue (vol, 64) == Catch::Approx (-60.0 + 72.0 * 64.0 / 127.0).margin (1e-5));

    const auto& hpf = info (guitarParam (GuitarParam::HpfHz));
    REQUIRE (ccToValue (hpf, 0) == Catch::Approx (20.0).margin (1e-3));
    REQUIRE (ccToValue (hpf, 127) == Catch::Approx (300.0).margin (1e-3));
    REQUIRE (ccToValue (hpf, 64) == Catch::Approx (20.0 * std::pow (15.0, 64.0 / 127.0)).margin (1e-3));

    const auto& tog = info (fxParam (0, FxParam::CrushOn));
    REQUIRE (ccToValue (tog, 63) == 0.f);
    REQUIRE (ccToValue (tog, 64) == 1.f);

    const auto& preset = info (fxParam (3, FxParam::Preset));
    for (int cc = 0; cc <= 127; ++cc)
        REQUIRE (ccToValue (preset, cc) == (cc <= 42 ? 0.f : cc <= 85 ? 1.f : 2.f));   // floor(cc * 3 / 128), see DEVIATIONS.md
    const auto& tap = info (guitarParam (GuitarParam::LoopTap));
    for (int cc = 0; cc <= 127; ++cc)
        REQUIRE (ccToValue (tap, cc) == (cc <= 63 ? 0.f : 1.f));

    for (int cc = 0; cc <= 127; ++cc)
    {
        REQUIRE (valueToCc (vol, ccToValue (vol, cc)) == cc);
        REQUIRE (std::abs (valueToCc (hpf, ccToValue (hpf, cc)) - cc) <= 1);
    }
    REQUIRE (valueToCc (tog, 0.f) == 0);
    REQUIRE (valueToCc (tog, 1.f) == 127);
    REQUIRE (valueToCc (preset, 0.f) == 21);
    REQUIRE (valueToCc (preset, 1.f) == 64);
    REQUIRE (valueToCc (preset, 2.f) == 107);
    REQUIRE (valueToCc (tap, 1.f) == 96);
}

namespace
{
    MidiMap mm4Map()
    {
        MidiMap m;
        bind (m, { { MidiSourceType::Cc, 0, 21 }, MidiTarget::ofParam (trackParam (7, TrackParam::Shakal)) });
        bind (m, { { MidiSourceType::Note, 0, 36 }, MidiTarget::ofAction (ActionId::TrackHit, 0) });
        bind (m, { { MidiSourceType::Note, 1, 37 }, MidiTarget::ofAction (ActionId::TogglePlay) });
        bind (m, { { MidiSourceType::Cc, 0, 64 }, MidiTarget::ofAction (ActionId::Fill) });
        bind (m, { { MidiSourceType::Cc, 0, 1 }, MidiTarget::ofAction (ActionId::Pad, 3) });
        return m;
    }
} // namespace

TEST_CASE ("MM4: dispatcher, note map building, binding conflicts", "[midi][model]")
{
    const MidiMap m = mm4Map();
    auto o = dispatch (m, { 0xB0, 21, 127 });
    REQUIRE (o.kind == MidiOutcome::Kind::Param);
    REQUIRE (o.param == trackParam (7, TrackParam::Shakal));
    REQUIRE (o.value == 1.f);
    o = dispatch (m, { 0xB4, 21, 127 });   // channel 5, binding is "any"
    REQUIRE (o.kind == MidiOutcome::Kind::Param);
    REQUIRE (dispatch (m, { 0x90, 36, 100 }).kind == MidiOutcome::Kind::None);   // the engine plays it

    o = dispatch (m, { 0x90, 37, 100 });
    REQUIRE (o.kind == MidiOutcome::Kind::Action);
    REQUIRE (o.action == ActionId::TogglePlay);
    REQUIRE (o.pressed);
    REQUIRE (dispatch (m, { 0x91, 37, 100 }).kind == MidiOutcome::Kind::None);   // channel 2
    REQUIRE (dispatch (m, { 0x90, 37, 0 }).kind == MidiOutcome::Kind::None);     // release of a non-momentary action

    o = dispatch (m, { 0xB0, 64, 100 });
    REQUIRE (o.kind == MidiOutcome::Kind::Action);
    REQUIRE (o.action == ActionId::Fill);
    REQUIRE (o.pressed);
    o = dispatch (m, { 0xB0, 64, 10 });
    REQUIRE (o.kind == MidiOutcome::Kind::Action);
    REQUIRE (! o.pressed);

    o = dispatch (m, { 0xB0, 1, 90 });
    REQUIRE (o.kind == MidiOutcome::Kind::Action);
    REQUIRE (o.action == ActionId::Pad);
    REQUIRE (o.index == 3);
    REQUIRE (o.pressed);
    REQUIRE (o.velocity == 90);

    MidiMap ch3 = m;
    ch3.channel = 3;
    REQUIRE (dispatch (ch3, { 0xB0, 21, 127 }).kind == MidiOutcome::Kind::None);

    // note map
    MidiMap nm;
    nm.channel = 4;
    MidiNoteMap built = buildNoteMap (nm);
    REQUIRE (built.channel == 4);
    for (int n = 0; n < 128; ++n)
    {
        const auto& b = built.notes[static_cast<std::size_t> (n)];
        if (n % 12 < 8)
        {
            REQUIRE (b.action == NoteAction::TrackHit);
            REQUIRE (b.index == n % 12);
        }
        else
            REQUIRE (b.action == NoteAction::None);
    }
    bind (nm, { { MidiSourceType::Note, 0, 36 }, MidiTarget::ofAction (ActionId::TogglePlay) });
    bind (nm, { { MidiSourceType::Note, 0, 37 }, MidiTarget::ofAction (ActionId::Pad, 3) });
    built = buildNoteMap (nm);
    REQUIRE (built.notes[36].action == NoteAction::None);
    REQUIRE (built.notes[37].action == NoteAction::Pad);
    REQUIRE (built.notes[37].index == 3);
    nm.defaultNotes = false;
    REQUIRE (buildNoteMap (nm).notes[38].action == NoteAction::None);

    // conflicts
    MidiMap c = m;
    REQUIRE (bind (c, { { MidiSourceType::Cc, 0, 21 }, MidiTarget::ofParam (masterParam (MasterParam::Gain)) }) == 1);
    REQUIRE (c.bindings.size() == m.bindings.size());
    REQUIRE (findByTarget (c, MidiTarget::ofParam (trackParam (7, TrackParam::Shakal))) == nullptr);
    REQUIRE (bind (c, { { MidiSourceType::Cc, 0, 99 }, MidiTarget::ofParam (masterParam (MasterParam::Gain)) }) == 1);
    REQUIRE (findByTarget (c, MidiTarget::ofParam (masterParam (MasterParam::Gain)))->source.number == 99);
}

TEST_CASE ("MM5: midimap.json round trip and tolerance", "[midi][model]")
{
    MidiMap m = mm4Map();
    m.channel = 3;
    m.defaultNotes = false;
    const auto text = toJson (m);
    auto r = midiMapFromJson (text);
    REQUIRE (r.ok);
    REQUIRE (r.skipped == 0);
    REQUIRE (r.map == m);
    REQUIRE (toJson (r.map) == text);

    const auto odd = midiMapFromJson (R"({ "format": "jackal-midimap", "version": 1, "channel": 0, "defaultNotes": true,
        "bindings": [ { "src": { "type": "cc", "ch": 0, "num": 21 }, "dst": { "param": "track.9.volume" } },
                      { "src": { "type": "cc", "ch": 0, "num": 22 }, "dst": { "action": "explode" } },
                      { "src": { "type": "cc", "ch": 1, "num": 64 }, "dst": { "action": "togglePlay" } },
                      { "src": { "type": "note", "ch": 0, "num": 36 }, "dst": { "action": "trackHit", "index": 0 } } ] })");
    REQUIRE (odd.ok);
    REQUIRE (odd.skipped == 2);
    REQUIRE (odd.map.bindings.size() == 2);

    REQUIRE (! midiMapFromJson (R"({ "format": "jackal-midimap", "version": 2 })").ok);
    REQUIRE (! midiMapFromJson (R"({ "format": "jackal-project", "version": 1 })").ok);
    const auto broken = midiMapFromJson ("{ this is not json");
    REQUIRE (! broken.ok);
    REQUIRE (! broken.error.empty());
}

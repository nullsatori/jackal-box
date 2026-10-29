#pragma once

#include "ParamSpec.h"

#include "Engine/MidiNoteMap.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace jackal
{

enum class MidiSourceType : uint8_t { Note, Cc };

struct MidiSource
{
    MidiSourceType type = MidiSourceType::Cc;
    uint8_t channel = 0;   // 0 = any
    uint8_t number = 0;
    bool operator== (const MidiSource&) const = default;
};

enum class ActionId : uint8_t
{
    Play, Stop, TogglePlay, Rec, Fill, Pattern /*index 0..7*/, SongMode, SongNext, SongPrev, LoopRec, LoopDub, LoopUndo,
    Pad /*0..7*/, TrackHit /*0..7*/, Mute /*0..7*/, Solo /*0..7*/, SelectTrack /*0..7*/, Mutate, Undo, Redo, Reroll, Capture,
    Count
};

constexpr bool actionIsMomentary (ActionId a) { return a == ActionId::Fill || a == ActionId::Pad || a == ActionId::TrackHit; }
constexpr bool actionInEngine (ActionId a) { return a == ActionId::TrackHit || a == ActionId::Pad || a == ActionId::Fill; }
constexpr bool actionHasIndex (ActionId a)
{
    return a == ActionId::Pattern || a == ActionId::Pad || a == ActionId::TrackHit || a == ActionId::Mute
        || a == ActionId::Solo || a == ActionId::SelectTrack;
}

std::string_view actionName (ActionId);
ActionId actionFromName (std::string_view, bool& ok);

struct MidiTarget
{
    enum class Kind : uint8_t { Param, Action };
    Kind kind = Kind::Param;
    ParamId param = 0;
    ActionId action = ActionId::Play;
    uint8_t index = 0;
    bool operator== (const MidiTarget&) const = default;

    static MidiTarget ofParam (ParamId id) { MidiTarget t; t.kind = Kind::Param; t.param = id; return t; }
    static MidiTarget ofAction (ActionId a, int index = 0)
    {
        MidiTarget t;
        t.kind = Kind::Action;
        t.action = a;
        t.index = static_cast<uint8_t> (index);
        return t;
    }
};

struct MidiBinding
{
    MidiSource source;
    MidiTarget target;
    bool operator== (const MidiBinding&) const = default;
};

struct MidiMap
{
    uint8_t channel = 0;        // 0 = omni
    bool defaultNotes = true;   // notes C..G of any octave hit tracks 1..8
    std::vector<MidiBinding> bindings;
    bool operator== (const MidiMap&) const = default;
};

// One target per source and one source per target: bind() removes conflicts and returns how many it removed.
int bind (MidiMap&, const MidiBinding&);
void unbindTarget (MidiMap&, const MidiTarget&);
void unbindSource (MidiMap&, const MidiSource&);
const MidiBinding* findByTarget (const MidiMap&, const MidiTarget&);

float ccToValue (const ParamInfo&, int cc);    // 0..127 -> value on the parameter's scale
int valueToCc (const ParamInfo&, float v);     // inverse, clamp 0..127
MidiNoteMap buildNoteMap (const MidiMap&);

// "track.3.shakal" or "action:pad:2"; parse returns false for unknown strings. For UI properties (midiTarget).
std::string targetToString (const MidiTarget&);
bool targetFromString (std::string_view, MidiTarget&);
std::string sourceToString (const MidiSource&);   // "CC 21", "NOTE 36 (ch 2)"

} // namespace jackal

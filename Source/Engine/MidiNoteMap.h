#pragma once

#include "PatternData.h"

#include <array>
#include <cstdint>

namespace jackal
{

enum class NoteAction : uint8_t { None, TrackHit, Pad, Fill };

struct NoteBinding
{
    NoteAction action = NoteAction::None;
    uint8_t index = 0;   // track 0..7 or pad 0..7
    bool operator== (const NoteBinding&) const = default;
};

struct MidiNoteMap
{
    uint8_t channel = 0;   // 0 = any, 1..16
    std::array<NoteBinding, 128> notes {};
    bool operator== (const MidiNoteMap&) const = default;
};

// The stage 2 map: note % 12 < 8 -> TrackHit (note % 12), any channel.
constexpr MidiNoteMap makeDefaultNoteMap() noexcept
{
    MidiNoteMap m {};
    for (int n = 0; n < 128; ++n)
        if (n % 12 < kNumTracks)
            m.notes[static_cast<std::size_t> (n)] = { NoteAction::TrackHit, static_cast<uint8_t> (n % 12) };
    return m;
}

inline constexpr MidiNoteMap kDefaultNoteMap = makeDefaultNoteMap();

} // namespace jackal

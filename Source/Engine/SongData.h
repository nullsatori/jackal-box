#pragma once

#include "PatternData.h"

#include <array>
#include <cstdint>

namespace jackal
{

constexpr int kMaxSongEntries = 32, kMaxRepeats = 16;

struct SongEntry
{
    int8_t pattern = -1;    // 0..7; -1 = empty entry (never inside length after validation)
    uint8_t repeats = 1;    // 1..16
    bool operator== (const SongEntry&) const = default;
};

// Trivially copyable, published by pointer like Pattern.
struct SongData
{
    uint8_t length = 0;     // entries 0..32
    bool loop = true;
    std::array<SongEntry, kMaxSongEntries> entries {};
    bool operator== (const SongData&) const = default;
};

constexpr bool songEntryValid (const SongData& s, int i) noexcept
{
    return i >= 0 && i < s.length && i < kMaxSongEntries
        && s.entries[static_cast<std::size_t> (i)].pattern >= 0
        && s.entries[static_cast<std::size_t> (i)].pattern < kNumPatterns;
}

} // namespace jackal

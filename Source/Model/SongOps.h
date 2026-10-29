#pragma once

#include "Engine/PatternData.h"
#include "Engine/SongData.h"

#include <array>

namespace jackal::songops
{

// Pure functions over SongData, no undo (the controller wraps them).
bool append (SongData&, int pattern, int repeats = 1);           // false when the song is full (32)
bool insert (SongData&, int at, int pattern, int repeats = 1);   // before `at`; at >= length appends
void remove (SongData&, int at);
void move (SongData&, int from, int to);                         // `from` ends up before the entry that was at `to`
void setRepeats (SongData&, int at, int repeats);                // clamp 1..16
void setPattern (SongData&, int at, int pattern);                // clamp 0..7
void clear (SongData&);
double totalBeats (const SongData&, const std::array<Pattern, kNumPatterns>&);   // sum of repeats * bars * 4
bool playable (const SongData&);                                 // length > 0 and every entry valid

} // namespace jackal::songops

#include "SongOps.h"

#include <algorithm>

namespace jackal::songops
{

namespace
{
    SongEntry entry (int pattern, int repeats)
    {
        return { static_cast<int8_t> (std::clamp (pattern, 0, kNumPatterns - 1)),
                 static_cast<uint8_t> (std::clamp (repeats, 1, kMaxRepeats)) };
    }
} // namespace

bool append (SongData& s, int pattern, int repeats)
{
    return insert (s, s.length, pattern, repeats);
}

bool insert (SongData& s, int at, int pattern, int repeats)
{
    if (s.length >= kMaxSongEntries)
        return false;
    const int i = std::clamp (at, 0, static_cast<int> (s.length));
    for (int k = s.length; k > i; --k)
        s.entries[static_cast<std::size_t> (k)] = s.entries[static_cast<std::size_t> (k - 1)];
    s.entries[static_cast<std::size_t> (i)] = entry (pattern, repeats);
    ++s.length;
    return true;
}

void remove (SongData& s, int at)
{
    if (at < 0 || at >= s.length)
        return;
    for (int k = at; k + 1 < s.length; ++k)
        s.entries[static_cast<std::size_t> (k)] = s.entries[static_cast<std::size_t> (k + 1)];
    --s.length;
    s.entries[s.length] = {};
}

void move (SongData& s, int from, int to)
{
    if (from < 0 || from >= s.length)
        return;
    const SongEntry e = s.entries[static_cast<std::size_t> (from)];
    int target = std::clamp (to, 0, static_cast<int> (s.length));
    remove (s, from);
    if (target > from)
        --target;
    insert (s, target, e.pattern, e.repeats);
}

void setRepeats (SongData& s, int at, int repeats)
{
    if (at >= 0 && at < s.length)
        s.entries[static_cast<std::size_t> (at)].repeats = static_cast<uint8_t> (std::clamp (repeats, 1, kMaxRepeats));
}

void setPattern (SongData& s, int at, int pattern)
{
    if (at >= 0 && at < s.length)
        s.entries[static_cast<std::size_t> (at)].pattern = static_cast<int8_t> (std::clamp (pattern, 0, kNumPatterns - 1));
}

void clear (SongData& s)
{
    const bool loop = s.loop;
    s = {};
    s.loop = loop;
}

double totalBeats (const SongData& s, const std::array<Pattern, kNumPatterns>& patterns)
{
    double beats = 0.0;
    for (int i = 0; i < s.length; ++i)
    {
        const auto& e = s.entries[static_cast<std::size_t> (i)];
        if (e.pattern < 0 || e.pattern >= kNumPatterns)
            continue;
        const int bars = std::clamp<int> (patterns[static_cast<std::size_t> (e.pattern)].bars, 1, kMaxBars);
        beats += static_cast<double> (std::max<int> (1, e.repeats) * bars * 4);
    }
    return beats;
}

bool playable (const SongData& s)
{
    if (s.length == 0)
        return false;
    for (int i = 0; i < s.length; ++i)
        if (! songEntryValid (s, i))
            return false;
    return true;
}

} // namespace jackal::songops

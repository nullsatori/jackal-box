#pragma once

#include "MidiMap.h"

#include <string>
#include <string_view>

namespace jackal
{

constexpr int kMidiMapFormatVersion = 1;

struct MidiMapParseResult
{
    bool ok = false;
    MidiMap map;
    std::string error;
    int skipped = 0;   // bindings with an unknown address or action
};

std::string toJson (const MidiMap&);
MidiMapParseResult midiMapFromJson (std::string_view);   // never throws

} // namespace jackal

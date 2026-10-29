#pragma once

#include "MidiMap.h"

#include "Engine/EngineTypes.h"

namespace jackal
{

struct MidiOutcome
{
    enum class Kind : uint8_t { None, Param, Action };
    Kind kind = Kind::None;
    ParamId param = 0;
    float value = 0.f;
    ActionId action {};
    uint8_t index = 0;
    bool pressed = false;
    uint8_t velocity = 0;
};

// Message thread, no state. Notes that the engine already handled through the note map give None.
MidiOutcome dispatch (const MidiMap&, const MidiEvent&);

} // namespace jackal

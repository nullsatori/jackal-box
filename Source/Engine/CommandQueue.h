#pragma once

#include "EngineTypes.h"
#include "SpscQueue.h"

namespace jackal
{

// UI (message thread) -> audio. Holds kCommandQueueCapacity - 1 commands.
using CommandQueue = SpscQueue<Command, static_cast<std::size_t> (kCommandQueueCapacity)>;
using MidiQueue    = SpscQueue<MidiEvent, 1024>;     // MIDI callback -> audio
using EventQueue   = SpscQueue<EngineEvent, 1024>;   // audio -> UI
using RetireQueue  = SpscQueue<Retired, 1024>;       // audio -> UI (pointers to delete)

} // namespace jackal

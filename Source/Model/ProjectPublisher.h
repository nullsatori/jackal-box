#pragma once

#include "Project.h"

#include "Engine/EngineCore.h"

#include <array>

namespace jackal
{

// Index chain * kSlotsPerChain + slot; ownership passes to the engine.
struct PublishedInserts
{
    std::array<InsertProcessor*, kNumChains * kSlotsPerChain> slots {};
};

// The load sequence of ProjectController: samples, tracks, params (track, fx x 10, master, guitar), mute/solo,
// seed, BPM, 8 patterns, inserts, song. Commands go to the core's queue and apply on its first process().
// samples[i] (ownership -> engine) may be nullptr.
void publishProject (EngineCore&, const Project&, const std::array<Sample*, kNumSampleSlots>& samples, const PublishedInserts&);

// The same parameter writes the controller does, without commands.
void writeFxParams (ParamTable&, int chain, const FxSettings&);

} // namespace jackal

#pragma once

#include "Engine/PatternData.h"

#include <array>
#include <cstdint>

namespace jackal
{

struct MutationInput
{
    std::array<bool, kNumTracks> sliced {};
    std::array<int, kNumTracks> numSlices {};
};

// Breaks the groove without wrecking it: only active steps change. Same inputs, same result.
void mutate (Pattern& p, const MutationInput&, float strength, uint64_t seed) noexcept;

} // namespace jackal

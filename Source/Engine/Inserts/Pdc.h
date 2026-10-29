#pragma once

#include "DelayLine.h"

#include "../PatternData.h"

#include <array>

namespace jackal
{

// Aligns the eight track buses to the slowest track chain.
class Pdc
{
public:
    void prepare (int maxDelay, int maxBlockSize);
    void reset() noexcept;
    // Delays bus t by (max - latencies[t]); returns max.
    int apply (float* const* busL, float* const* busR, int n, const std::array<int, kNumTracks>& latencies) noexcept;

private:
    std::array<DelayLine, kNumTracks> lines_ {};
    int maxDelay_ = 0;
};

} // namespace jackal

#pragma once

#include "../StepHash.h"

#include <cstdint>

namespace jackal
{

// Salts start at 0x10 so they never collide with pattern indices 0..7 used by step probability.
struct FxHash
{
    static constexpr int kSaltFlip = 0x10, kSaltFlipBurst = 0x11, kSaltGlitchGate = 0x12, kSaltGlitchType = 0x13;

    static double unit (uint32_t seed, int salt, int chain, int64_t pos) noexcept
    {
        return StepHash::unit (seed, salt, chain, pos);
    }
};

} // namespace jackal

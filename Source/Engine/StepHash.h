#pragma once

#include <cstdint>

namespace jackal
{

struct SplitMix64
{
    uint64_t state = 0;

    explicit SplitMix64 (uint64_t seed) noexcept : state (seed) {}

    uint64_t next() noexcept
    {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    double unit() noexcept { return static_cast<double> (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// Stateless randomness for step probability: same inputs, same answer, any buffer size.
struct StepHash
{
    static double unit (uint32_t seed, int pattern, int track, int64_t k) noexcept
    {
        const uint64_t key = (static_cast<uint64_t> (seed) << 32)
                           ^ (static_cast<uint64_t> (pattern & 0xFF) << 24)
                           ^ (static_cast<uint64_t> (track & 0xFF) << 16)
                           ^ (static_cast<uint64_t> (k) * 0x9E3779B97F4A7C15ull);
        SplitMix64 rng (key);
        return rng.unit();
    }
};

} // namespace jackal

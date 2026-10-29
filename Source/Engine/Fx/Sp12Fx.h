#pragma once

#include "Biquad.h"
#include "FxParams.h"

#include <array>

namespace jackal
{

// 26.04 kHz, 12 bit, 4th-order Butterworth output filter (two RBJ sections).
class Sp12Fx
{
public:
    static constexpr double kRate = 26040.0;
    static constexpr float kLevels = 2048.f;

    void prepare (double sampleRate, int);
    void reset() noexcept;
    void process (float* left, float* right, int n, const FxState&, const FxContext&) noexcept;

    float filterState (int ch, int section, int which) const noexcept
    {
        const auto& b = filters_[static_cast<std::size_t> (ch * 2 + section)];
        return which == 0 ? b.s1 : b.s2;
    }

private:
    double sampleRate_ = 48000.0, acc_ = 0.0;
    std::array<float, 2> held_ {};
    std::array<Biquad, 4> filters_ {};   // L1, L2, R1, R2
    float filterHz_ = -1.f;
};

} // namespace jackal

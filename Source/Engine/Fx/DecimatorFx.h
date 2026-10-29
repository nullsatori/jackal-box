#pragma once

#include "FxParams.h"

#include <array>

namespace jackal
{

// Sample-and-hold on a phase accumulator, one accumulator for both channels. No filter.
class DecimatorFx
{
public:
    void prepare (double sampleRate, int);
    void reset() noexcept;
    void process (float* left, float* right, int n, const FxState&, const FxContext&) noexcept;

private:
    double sampleRate_ = 48000.0, acc_ = 0.0;
    std::array<float, 2> held_ {};
};

} // namespace jackal

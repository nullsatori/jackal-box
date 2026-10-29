#pragma once

#include "FxParams.h"

namespace jackal
{

// ADC overload: gain, then clip or wrap. No level compensation.
class DriveFx
{
public:
    void prepare (double, int) {}
    void reset() noexcept {}
    void process (float* left, float* right, int n, const FxState&, const FxContext&) noexcept;
};

} // namespace jackal

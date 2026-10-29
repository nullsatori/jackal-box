#pragma once

#include "FxParams.h"

namespace jackal
{

// Linear or mu-law quantisation, continuous bit depth 1..16. No dither.
class BitcrushFx
{
public:
    void prepare (double, int) {}
    void reset() noexcept {}
    void process (float* left, float* right, int n, const FxState&, const FxContext&) noexcept;
};

} // namespace jackal

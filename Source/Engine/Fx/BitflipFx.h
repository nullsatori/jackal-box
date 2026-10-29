#pragma once

#include "FxParams.h"

namespace jackal
{

// XOR one bit of the int16 value. One decision for both channels; untouched samples stay float-exact.
class BitflipFx
{
public:
    void prepare (double, int) {}
    void reset() noexcept {}
    void process (float* left, float* right, int n, const FxState&, const FxContext&) noexcept;
};

} // namespace jackal

#include "DecimatorFx.h"

#include <cmath>

namespace jackal
{

void DecimatorFx::prepare (double sampleRate, int)
{
    sampleRate_ = sampleRate;
    reset();
}

void DecimatorFx::reset() noexcept
{
    acc_ = 0.0;
    held_ = {};
}

void DecimatorFx::process (float* left, float* right, int n, const FxState& s, const FxContext&) noexcept
{
    if (! s.decimOn)
        return;

    const double inc = static_cast<double> (s.decimRateHz) / sampleRate_;
    float* ch[2] = { left, right };

    for (int i = 0; i < n; ++i)
    {
        acc_ += inc;
        const bool latch = acc_ >= 1.0;
        if (latch)
            acc_ -= std::floor (acc_);   // rate >= sr latches every sample, the accumulator stays bounded

        for (int c = 0; c < 2; ++c)
        {
            float* p = ch[c];
            if (p == nullptr)
                continue;
            if (latch)
                held_[static_cast<std::size_t> (c)] = p[i];
            p[i] = held_[static_cast<std::size_t> (c)];
        }
    }
}

} // namespace jackal

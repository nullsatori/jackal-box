#include "Sp12Fx.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void Sp12Fx::prepare (double sampleRate, int)
{
    sampleRate_ = sampleRate;
    reset();
}

void Sp12Fx::reset() noexcept
{
    acc_ = 0.0;
    held_ = {};
    for (auto& f : filters_)
        f.reset();
    filterHz_ = -1.f;
}

void Sp12Fx::process (float* left, float* right, int n, const FxState& s, const FxContext& ctx) noexcept
{
    if (! s.sp12On)
        return;

    const bool filter = s.sp12FilterHz < 20000.f;
    if (filter && s.sp12FilterHz != filterHz_)
    {
        filterHz_ = s.sp12FilterHz;
        for (int c = 0; c < 2; ++c)
        {
            filters_[static_cast<std::size_t> (c * 2)].setLowpass (sampleRate_, filterHz_, 0.5412);
            filters_[static_cast<std::size_t> (c * 2 + 1)].setLowpass (sampleRate_, filterHz_, 1.3066);
        }
    }
    if (ctx.rampPhase == 0)   // control-block boundary
        for (auto& f : filters_)
            f.flushDenormals();

    const bool hold = kRate < sampleRate_;
    const double inc = kRate / sampleRate_;
    float* ch[2] = { left, right };

    for (int i = 0; i < n; ++i)
    {
        bool latch = false;
        if (hold)
        {
            acc_ += inc;
            if (acc_ >= 1.0)
            {
                acc_ -= 1.0;
                latch = true;
            }
        }

        for (int c = 0; c < 2; ++c)
        {
            float* p = ch[c];
            if (p == nullptr)
                continue;
            const auto ci = static_cast<std::size_t> (c);
            if (hold)
            {
                if (latch)
                    held_[ci] = std::clamp (std::round (std::clamp (p[i], -1.f, 1.f) * kLevels) / kLevels, -1.f, 2047.f / 2048.f);
            }
            else
            {
                held_[ci] = std::clamp (std::round (std::clamp (p[i], -1.f, 1.f) * kLevels) / kLevels, -1.f, 2047.f / 2048.f);
            }

            float y = held_[ci];
            if (filter)
                y = filters_[ci * 2 + 1].process (filters_[ci * 2].process (y));
            p[i] = y;
        }
    }
}

} // namespace jackal

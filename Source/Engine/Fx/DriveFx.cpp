#include "DriveFx.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void DriveFx::process (float* left, float* right, int n, const FxState& s, const FxContext& ctx) noexcept
{
    if (! s.driveOn)
        return;

    const float g0 = ctx.driveGainStart, g1 = ctx.driveGainEnd;
    const bool ramp = g0 != g1;
    const float len = static_cast<float> (std::max (1, ctx.rampLength));
    float* ch[2] = { left, right };

    for (int i = 0; i < n; ++i)
    {
        float g = g1;
        if (ramp)
        {
            const float f = std::min (1.f, static_cast<float> (ctx.rampPhase + i + 1) / len);
            g = g0 + (g1 - g0) * f;
        }

        for (float* c : ch)
        {
            if (c == nullptr)
                continue;
            const float v = c[i] * g;
            c[i] = s.driveWrap ? v - 2.f * std::floor ((v + 1.f) * 0.5f) : std::clamp (v, -1.f, 1.f);
        }
    }
}

} // namespace jackal

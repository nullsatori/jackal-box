#include "BitflipFx.h"

#include "FxHash.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jackal
{

void BitflipFx::process (float* left, float* right, int n, const FxState& s, const FxContext& ctx) noexcept
{
    if (! s.flipOn || s.flipProb <= 0.f)
        return;

    const double prob = static_cast<double> (s.flipProb);
    const int32_t mask = int32_t (1) << std::clamp (s.flipBit, 0, 15);
    float* ch[2] = { left, right };

    for (int i = 0; i < n; ++i)
    {
        const int64_t p = ctx.pos + i;
        const double u = s.flipBurst ? FxHash::unit (ctx.seed, FxHash::kSaltFlipBurst, ctx.chainIndex, p >> 6)
                                     : FxHash::unit (ctx.seed, FxHash::kSaltFlip, ctx.chainIndex, p);
        if (u >= prob)
            continue;

        for (float* c : ch)
        {
            if (c == nullptr)
                continue;
            int32_t v = static_cast<int32_t> (std::lround (std::clamp (c[i], -1.f, 1.f) * 32767.f));
            v ^= mask;
            const auto w = static_cast<int16_t> (v);   // C++20: modular
            c[i] = static_cast<float> (w) / 32767.f;
        }
    }
}

} // namespace jackal

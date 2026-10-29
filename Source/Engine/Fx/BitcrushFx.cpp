#include "BitcrushFx.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void BitcrushFx::process (float* left, float* right, int n, const FxState& s, const FxContext&) noexcept
{
    if (! s.crushOn)
        return;

    const float b = std::clamp (s.crushBits, 1.f, 16.f);
    const float h = std::exp2 (b - 1.f);
    constexpr float mu = 255.f;
    const float lnMu = std::log1p (mu);
    float* ch[2] = { left, right };

    for (float* p : ch)
    {
        if (p == nullptr)
            continue;
        for (int i = 0; i < n; ++i)
        {
            const float xc = std::clamp (p[i], -1.f, 1.f);
            if (! s.crushMulaw)
            {
                p[i] = std::clamp (std::round (xc * h) / h, -1.f, 1.f);
                continue;
            }
            const float sg = xc < 0.f ? -1.f : 1.f;
            const float c = sg * std::log1p (mu * std::fabs (xc)) / lnMu;
            const float cq = std::clamp (std::round (c * h) / h, -1.f, 1.f);
            const float sq = cq < 0.f ? -1.f : 1.f;
            p[i] = cq == 0.f ? 0.f : sq * (std::pow (1.f + mu, std::fabs (cq)) - 1.f) / mu;
        }
    }
}

} // namespace jackal

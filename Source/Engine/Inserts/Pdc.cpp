#include "Pdc.h"

#include <algorithm>

namespace jackal
{

void Pdc::prepare (int maxDelay, int maxBlockSize)
{
    maxDelay_ = maxDelay;
    for (auto& l : lines_)
        l.prepare (maxDelay, maxBlockSize);
}

void Pdc::reset() noexcept
{
    for (auto& l : lines_)
        l.reset();
}

int Pdc::apply (float* const* busL, float* const* busR, int n, const std::array<int, kNumTracks>& latencies) noexcept
{
    int mx = 0;
    for (int v : latencies)
        mx = std::max (mx, std::clamp (v, 0, maxDelay_));
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto i = static_cast<std::size_t> (t);
        lines_[i].process (busL[t], busR[t], n, mx - std::clamp (latencies[i], 0, maxDelay_));
    }
    return mx;
}

} // namespace jackal

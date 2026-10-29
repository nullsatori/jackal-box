#include "InsertChain.h"

namespace jackal
{

void InsertChain::prepare (double sampleRate, int maxBlockSize, int chainIndex)
{
    chain_ = chainIndex;
    for (auto& s : slots_)
        s.prepare (sampleRate, maxBlockSize);
}

void InsertChain::reset() noexcept
{
    for (auto& s : slots_)
        s.reset();
}

void InsertChain::process (float* left, float* right, int n, const ParamTable& params, const InsertContext& ctx) noexcept
{
    for (int i = 0; i < kSlots; ++i)
    {
        const bool bypass = params.get (insertParam (chain_, i, InsertParam::Bypass)) >= 0.5f;
        slot (i).process (left, right, n, bypass, ctx);
    }
}

int InsertChain::latencySamples() const noexcept
{
    int sum = 0;
    for (const auto& s : slots_)
        sum += s.latencySamples();
    return sum;
}

bool InsertChain::latencyOverflow() const noexcept
{
    for (const auto& s : slots_)
        if (s.latencyOverflow())
            return true;
    return false;
}

} // namespace jackal

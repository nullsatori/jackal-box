#include "InsertSlot.h"

#include <algorithm>

namespace jackal
{

void InsertSlot::prepare (double sampleRate, int maxBlockSize)
{
    bypassDelay_.prepare (kMaxPdcSamplesSlot, maxBlockSize);
    if (auto* p = get())
        p->prepare (sampleRate, maxBlockSize);
}

InsertProcessor* InsertSlot::swap (InsertProcessor* next) noexcept
{
    bypassDelay_.reset();
    return proc_.exchange (next, std::memory_order_acq_rel);
}

int InsertSlot::latencySamples() const noexcept
{
    const auto* p = get();
    return p == nullptr ? 0 : std::clamp (p->latencySamples(), 0, kMaxPdcSamplesSlot);
}

bool InsertSlot::latencyOverflow() const noexcept
{
    const auto* p = get();
    return p != nullptr && p->latencySamples() > kMaxPdcSamplesSlot;
}

void InsertSlot::reset() noexcept
{
    bypassDelay_.reset();
    if (auto* p = get())
        p->reset();
}

void InsertSlot::process (float* left, float* right, int n, bool bypass, const InsertContext& ctx) noexcept
{
    auto* p = get();
    if (p == nullptr)
        return;
    if (bypass)
        bypassDelay_.process (left, right, n, latencySamples());
    else
        p->process (left, right, n, ctx);
}

} // namespace jackal

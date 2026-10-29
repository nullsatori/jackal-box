#pragma once

#include "DelayLine.h"
#include "InsertProcessor.h"

#include <atomic>

namespace jackal
{

constexpr int kMaxPdcSamplesSlot = 16384;

// One insert: the processor pointer plus a delay line, so bypass keeps the latency the plugin reports.
class InsertSlot
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    InsertProcessor* swap (InsertProcessor* next) noexcept;   // audio thread; returns the old one
    InsertProcessor* get() const noexcept { return proc_.load (std::memory_order_acquire); }
    int latencySamples() const noexcept;
    bool latencyOverflow() const noexcept;
    void reset() noexcept;
    void process (float* left, float* right, int n, bool bypass, const InsertContext&) noexcept;

private:
    std::atomic<InsertProcessor*> proc_ { nullptr };
    DelayLine bypassDelay_;
};

} // namespace jackal

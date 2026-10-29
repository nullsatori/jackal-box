#pragma once

#include "PatternData.h"
#include "Sample.h"

#include <array>
#include <atomic>

namespace jackal
{

class SampleBank
{
public:
    // Audio thread. Returns the old pointer (may be nullptr); the caller must retire it.
    const Sample* swap (int slot, const Sample* next) noexcept
    {
        if (slot < 0 || slot >= kNumSampleSlots)
            return nullptr;
        return slots_[static_cast<std::size_t> (slot)].exchange (next, std::memory_order_acq_rel);
    }

    const Sample* get (int slot) const noexcept
    {
        if (slot < 0 || slot >= kNumSampleSlots)
            return nullptr;
        return slots_[static_cast<std::size_t> (slot)].load (std::memory_order_acquire);
    }

private:
    std::array<std::atomic<const Sample*>, kNumSampleSlots> slots_ {};
};

} // namespace jackal

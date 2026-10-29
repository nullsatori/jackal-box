#pragma once

#include "InsertSlot.h"

#include "../ParamIds.h"

#include <array>

namespace jackal
{

// The two insert slots of one chain (track 0..7, master 8, guitar 9).
class InsertChain
{
public:
    static constexpr int kSlots = 2;

    void prepare (double sampleRate, int maxBlockSize, int chainIndex);
    void reset() noexcept;
    void process (float* left, float* right, int n, const ParamTable&, const InsertContext&) noexcept;
    int latencySamples() const noexcept;
    bool latencyOverflow() const noexcept;
    InsertSlot& slot (int i) noexcept { return slots_[static_cast<std::size_t> (i)]; }
    const InsertSlot& slot (int i) const noexcept { return slots_[static_cast<std::size_t> (i)]; }

private:
    std::array<InsertSlot, kSlots> slots_ {};
    int chain_ = 0;
};

} // namespace jackal

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace jackal
{

// Immutable after creation. Built outside the audio thread; the engine only reads it.
struct Sample
{
    int numChannels = 1;                       // 1 or 2
    double sampleRate = 48000.0;
    int64_t length = 0;                        // samples per channel
    std::array<std::vector<float>, 2> data;    // deinterleaved; data[1] empty for mono
    std::vector<int64_t> sliceStarts;          // empty = not sliced; else numSlices + 1 entries, last == length
    double sourceBars = 0.0;                   // 0 = not a loop

    int numSlices() const noexcept
    {
        return sliceStarts.size() > 1 ? static_cast<int> (sliceStarts.size()) - 1 : 0;
    }

    const float* channel (int ch) const noexcept
    {
        return (ch >= numChannels || ch < 0) ? data[0].data() : data[static_cast<std::size_t> (ch)].data();
    }
};

} // namespace jackal

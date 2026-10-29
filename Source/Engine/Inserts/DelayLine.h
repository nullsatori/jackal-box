#pragma once

#include <cstdint>
#include <vector>

namespace jackal
{

// Stereo delay with a fixed capacity. delay == 0 is a bit-exact passthrough.
class DelayLine
{
public:
    void prepare (int maxDelay, int maxBlockSize);
    void reset() noexcept;
    void process (float* left, float* right, int n, int delay) noexcept;
    int maxDelay() const noexcept { return maxDelay_; }

private:
    std::vector<float> ring_[2];
    int64_t size_ = 1, w_ = 0;
    int maxDelay_ = 0;
};

} // namespace jackal

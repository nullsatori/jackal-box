#include "DelayLine.h"

#include <algorithm>

namespace jackal
{

void DelayLine::prepare (int maxDelay, int maxBlockSize)
{
    maxDelay_ = std::max (0, maxDelay);
    size_ = static_cast<int64_t> (maxDelay_) + std::max (1, maxBlockSize) + 1;
    for (auto& r : ring_)
        r.assign (static_cast<std::size_t> (size_), 0.f);
    w_ = 0;
}

void DelayLine::reset() noexcept
{
    for (auto& r : ring_)
        std::fill (r.begin(), r.end(), 0.f);
    w_ = 0;
}

void DelayLine::process (float* left, float* right, int n, int delay) noexcept
{
    delay = std::clamp (delay, 0, maxDelay_);
    float* ch[2] = { left, right };

    // the ring is written even at delay 0, so a later delay change reads real history
    for (int i = 0; i < n; ++i)
    {
        const int64_t wi = (w_ + i) % size_;
        const int64_t ri = ((w_ + i - delay) % size_ + size_) % size_;
        for (int c = 0; c < 2; ++c)
        {
            float* p = ch[c];
            auto& r = ring_[c];
            const float x = p != nullptr ? p[i] : 0.f;
            r[static_cast<std::size_t> (wi)] = x;
            if (p != nullptr && delay > 0)
                p[i] = r[static_cast<std::size_t> (ri)];
        }
    }
    w_ = (w_ + n) % size_;
}

} // namespace jackal

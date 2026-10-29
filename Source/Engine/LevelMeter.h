#pragma once

#include <atomic>
#include <cmath>

namespace jackal
{

// Audio thread writes peak/rms once per block; UI reads on its own timer
// and does decay / peak-hold itself.
class LevelMeter
{
public:
    void update (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        float peak = 0.f;
        double sumSquares = 0.0;
        long count = 0;

        if (channels != nullptr)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* data = channels[ch];
                if (data == nullptr)
                    continue;

                for (int i = 0; i < numSamples; ++i)
                {
                    const float v = data[i];
                    const float a = std::fabs (v);
                    if (a > peak)
                        peak = a;
                    sumSquares += static_cast<double> (v) * static_cast<double> (v);
                }
                count += numSamples;
            }
        }

        const double rms = count > 0 ? std::sqrt (sumSquares / static_cast<double> (count)) : 0.0;
        peak_.store (peak, std::memory_order_relaxed);
        rms_.store (static_cast<float> (rms), std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        peak_.store (0.f, std::memory_order_relaxed);
        rms_.store (0.f, std::memory_order_relaxed);
    }

    float peak() const noexcept { return peak_.load (std::memory_order_relaxed); }
    float rms() const noexcept  { return rms_.load (std::memory_order_relaxed); }

private:
    std::atomic<float> peak_ { 0.f };
    std::atomic<float> rms_ { 0.f };
};

} // namespace jackal

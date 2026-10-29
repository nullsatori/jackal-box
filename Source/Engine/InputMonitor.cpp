#include "InputMonitor.h"

#include <cmath>

namespace jackal
{

void InputMonitor::prepare (double sampleRate) noexcept
{
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    smoothingCoeff_ = 1.0 - std::exp (-1.0 / (0.005 * sr));
    gainInitialised_ = false;
    currentGain_ = 0.f;
}

void InputMonitor::setInputChannel (int channel) noexcept
{
    channel_ = channel;
}

void InputMonitor::process (const float* const* in, int numIn, float* const* out, int numOut,
                            int numSamples, float gainLinear, bool enabled) noexcept
{
    if (! enabled)
    {
        // next enable fades in from silence instead of jumping
        currentGain_ = 0.f;
        gainInitialised_ = true;
        return;
    }

    if (! gainInitialised_)
    {
        currentGain_ = gainLinear;
        gainInitialised_ = true;
    }

    if (in == nullptr || channel_ < 0 || channel_ >= numIn || in[channel_] == nullptr)
        return;

    const float* source = in[channel_];
    const auto coeff = static_cast<float> (smoothingCoeff_);
    float gain = currentGain_;

    for (int i = 0; i < numSamples; ++i)
    {
        gain += (gainLinear - gain) * coeff;
        const float v = source[i] * gain;

        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
                out[ch][i] += v;
    }

    currentGain_ = gain;
}

} // namespace jackal

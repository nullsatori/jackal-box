#include "NoiseGate.h"

#include <cmath>

namespace jackal
{

namespace
{
    // one-pole coefficient that falls 60 dB in `seconds`
    float coeff60 (double seconds, double sr) noexcept
    {
        return static_cast<float> (1.0 - std::exp (-6.907755 / (seconds * sr)));
    }
} // namespace

void NoiseGate::prepare (double sampleRate) noexcept
{
    const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    aAtt_ = coeff60 (0.001, sr);
    aRel_ = coeff60 (0.050, sr);
    aGain_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * sr)));
    reset();
}

void NoiseGate::reset() noexcept
{
    env_ = 1.f;
    gain_ = 1.f;
    open_ = true;
}

void NoiseGate::process (float* mono, int n, float thresholdDb) noexcept
{
    if (thresholdDb <= -80.f || mono == nullptr)
        return;

    const float thr = std::pow (10.f, thresholdDb / 20.f);
    for (int i = 0; i < n; ++i)
    {
        const float a = std::fabs (mono[i]);
        env_ += (a > env_ ? aAtt_ : aRel_) * (a - env_);
        if (open_ && env_ < thr * 0.5f)
            open_ = false;
        else if (! open_ && env_ > thr)
            open_ = true;

        const float target = open_ ? 1.f : 0.f;
        gain_ += aGain_ * (target - gain_);
        if (std::fabs (target - gain_) < (open_ ? 1e-6f : 1e-4f))
            gain_ = target;
        mono[i] *= gain_;
    }
}

} // namespace jackal

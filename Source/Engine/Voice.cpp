#include "Voice.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    float dbToGain (float db) noexcept
    {
        return db <= -60.f ? 0.f : std::pow (10.f, db / 20.f);
    }
} // namespace

void Voice::prepare (double sampleRate) noexcept
{
    engineRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    active_ = false;
}

void Voice::start (const TriggerParams& p) noexcept
{
    active_ = false;
    fadeLeft_ = 0;
    fadeTotal_ = 0;

    if (p.sample == nullptr || p.sample->length <= 0 || p.sample->data[0].empty())
        return;

    p_ = p;
    p_.startSample = std::clamp<int64_t> (p.startSample, 0, p.sample->length);
    p_.endSample = std::clamp<int64_t> (p.endSample, p_.startSample, p.sample->length);
    if (p_.endSample <= p_.startSample)
        return;

    rate_ = (p.sample->sampleRate / engineRate_) * std::pow (2.0, static_cast<double> (p.pitchSemitones) / 12.0) * p.rateFit;
    if (! (rate_ > 0.0) || ! std::isfinite (rate_))
        return;

    pos_ = p_.reverse ? static_cast<double> (p_.endSample - 1) : static_cast<double> (p_.startSample);

    useEnv_ = p.decayMs < 5000.f;
    env_ = 1.0;
    if (useEnv_)
    {
        const double decayMs = std::max (5.0, static_cast<double> (p.decayMs));
        const double tauSamples = decayMs / 6.907755278982137 * 0.001 * engineRate_;
        envMul_ = std::exp (-1.0 / tauSamples);
    }

    useTone_ = p.toneHz < 20000.f;
    if (useTone_)
    {
        const double f = std::max (20.0, static_cast<double> (p.toneHz));
        toneA_ = static_cast<float> (1.0 - std::exp (-2.0 * kPi * f / engineRate_));
    }
    toneL_ = toneR_ = 0.f;

    const double pan = std::clamp (static_cast<double> (p.pan), -1.0, 1.0);
    const float vol = dbToGain (p.volumeDb) * p.gain;
    // hard pan is exact silence on the other side (cos(pi/2) is not exactly 0 in floating point)
    const double gl = pan >= 1.0 ? 0.0 : std::cos ((pan + 1.0) * kPi / 4.0);
    const double gr = pan <= -1.0 ? 0.0 : std::sin ((pan + 1.0) * kPi / 4.0);
    gainL_ = static_cast<float> (gl) * vol;
    gainR_ = static_cast<float> (gr) * vol;

    attackPos_ = 0;
    active_ = true;
}

void Voice::release (int fadeSamples) noexcept
{
    if (! active_ || fadeTotal_ > 0)
        return;
    fadeTotal_ = std::max (1, fadeSamples);
    fadeLeft_ = fadeTotal_;
}

void Voice::kill() noexcept
{
    active_ = false;
    fadeLeft_ = 0;
    fadeTotal_ = 0;
}

void Voice::render (float* left, float* right, int from, int to) noexcept
{
    if (! active_)
        return;

    const Sample& s = *p_.sample;
    const float* d0 = s.channel (0);
    const float* d1 = s.channel (1);
    const bool stereo = s.numChannels > 1;
    const int64_t start = p_.startSample, end = p_.endSample;

    for (int i = from; i < to; ++i)
    {
        if (p_.reverse ? pos_ < static_cast<double> (start) : pos_ >= static_cast<double> (end))
        {
            active_ = false;
            return;
        }

        const auto i0 = static_cast<int64_t> (std::floor (pos_));
        const float frac = static_cast<float> (pos_ - static_cast<double> (i0));
        const bool hasNext = i0 + 1 < end;

        float l = d0[i0];
        if (frac != 0.f)
            l += ((hasNext ? d0[i0 + 1] : 0.f) - l) * frac;

        float r = l;
        if (stereo)
        {
            r = d1[i0];
            if (frac != 0.f)
                r += ((hasNext ? d1[i0 + 1] : 0.f) - r) * frac;
        }

        if (useTone_)
        {
            toneL_ += toneA_ * (l - toneL_);
            toneR_ += toneA_ * (r - toneR_);
            l = toneL_;
            r = toneR_;
        }

        float g = 1.f;
        if (attackPos_ < p_.attackSamples)
        {
            g = static_cast<float> (attackPos_ + 1) / static_cast<float> (p_.attackSamples);
            ++attackPos_;
        }

        if (useEnv_)
        {
            g *= static_cast<float> (env_);
            env_ *= envMul_;
        }

        if (fadeTotal_ > 0)
        {
            --fadeLeft_;
            g *= static_cast<float> (fadeLeft_) / static_cast<float> (fadeTotal_);
        }

        left[i] += l * g * gainL_;
        right[i] += r * g * gainR_;

        pos_ += p_.reverse ? -rate_ : rate_;

        if ((useEnv_ && env_ < 1e-4) || (fadeTotal_ > 0 && fadeLeft_ <= 0))
        {
            active_ = false;
            fadeTotal_ = 0;
            return;
        }
    }
}

} // namespace jackal

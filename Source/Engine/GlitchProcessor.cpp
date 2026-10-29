#include "GlitchProcessor.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

namespace
{
    constexpr float kFadeStep = 1.f / 64.f;
}

void GlitchProcessor::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    ringSize_ = static_cast<int64_t> (std::ceil (sampleRate_ * 4.0 * 60.0 / kMinBpm)) + std::max (1, maxBlockSize);
    ringL_.assign (static_cast<std::size_t> (ringSize_), 0.f);
    ringR_.assign (static_cast<std::size_t> (ringSize_), 0.f);
    clock_ = 0;
    reset();
}

void GlitchProcessor::reset() noexcept
{
    clock_ = 0;
    numEvents_ = 0;
    active_ = pending_ = -1;
    releasing_ = false;
    mix_ = 0.f;
    holdCount_ = 0;
    std::fill (ringL_.begin(), ringL_.end(), 0.f);
    std::fill (ringR_.begin(), ringR_.end(), 0.f);
}

void GlitchProcessor::padDown (int offset, int pad, const Transport& transport, int64_t blockStart) noexcept
{
    if (pad < 0 || pad >= 8 || numEvents_ >= static_cast<int> (events_.size()))
        return;

    int64_t start = clock_ + offset;

    if (transport.isPlaying())
    {
        const int64_t abs = blockStart + offset;
        double sixteenth = std::floor (transport.beatAtSample (abs) * 4.0);
        int64_t at = transport.sampleAtBeat (sixteenth / 4.0);
        while (at < abs)
        {
            sixteenth += 1.0;
            at = transport.sampleAtBeat (sixteenth / 4.0);
        }
        start = clock_ + (at - blockStart);
    }

    events_[static_cast<std::size_t> (numEvents_++)] = { start, pad, true };
}

void GlitchProcessor::padUp (int offset, int pad) noexcept
{
    if (numEvents_ >= static_cast<int> (events_.size()))
        return;
    events_[static_cast<std::size_t> (numEvents_++)] = { clock_ + offset, pad, false };
}

float GlitchProcessor::readRingInt (const std::vector<float>& ring, int64_t pos) const noexcept
{
    int64_t i = pos % ringSize_;
    if (i < 0)
        i += ringSize_;
    return ring[static_cast<std::size_t> (i)];
}

float GlitchProcessor::readRing (const std::vector<float>& ring, double pos) const noexcept
{
    const double f = std::floor (pos);
    const auto i0 = static_cast<int64_t> (f);
    const float frac = static_cast<float> (pos - f);
    const float a = readRingInt (ring, i0);
    if (frac == 0.f || i0 + 1 > clock_)
        return a;
    return a + (readRingInt (ring, i0 + 1) - a) * frac;
}

void GlitchProcessor::process (float* left, float* right, int n, const Transport& transport, int64_t /*blockStart*/) noexcept
{
    const double spb = transport.samplesPerBeat();
    int nextEvent = 0;

    for (int i = 0; i < n; ++i, ++clock_)
    {
        const int64_t t = clock_;

        // pad events are queued in time order by their producers (commands at 0, pattern lane in order)
        for (; nextEvent < numEvents_; ++nextEvent)
        {
            const auto& e = events_[static_cast<std::size_t> (nextEvent)];
            if (e.down)
            {
                // a down event is "applied" at its arrival; it becomes pending until its quantized start
                pending_ = e.pad;
                pendingStart_ = e.time;
            }
            else
            {
                if (e.time > t)
                    break;
                if (pending_ == e.pad)
                    pending_ = -1;
                if (active_ == e.pad)
                    releasing_ = true;
            }
        }

        if (pending_ >= 0 && t >= pendingStart_)
        {
            active_ = pending_;
            pending_ = -1;
            startA_ = t;
            readPos_ = static_cast<double> (t);
            releasing_ = false;
            holdCount_ = 0;
        }

        const float inL = left[i], inR = right[i];
        const auto w = static_cast<std::size_t> (t % ringSize_);
        ringL_[w] = inL;
        ringR_[w] = inR;

        if (active_ < 0)
            continue;

        const double e = static_cast<double> (t - startA_);
        float wetL = inL, wetR = inR;

        switch (static_cast<GlitchPad> (active_))
        {
            case GlitchPad::Stutter16:
            case GlitchPad::Stutter32:
            case GlitchPad::Stutter64:
            {
                const double div = active_ == 0 ? 4.0 : (active_ == 1 ? 8.0 : 16.0);
                const int64_t len = std::max<int64_t> (1, std::llround (spb / div));
                const int64_t r = startA_ + (t - startA_) % len;
                wetL = readRingInt (ringL_, r);
                wetR = readRingInt (ringR_, r);
                break;
            }
            case GlitchPad::Reverse:
            {
                const int64_t len = std::max<int64_t> (1, std::llround (spb));
                const int64_t r = startA_ - 1 - (t - startA_) % len;
                wetL = readRingInt (ringL_, r);
                wetR = readRingInt (ringR_, r);
                break;
            }
            case GlitchPad::TapeStop:
            {
                const double dur = 2.0 * spb;
                if (e >= dur)
                {
                    wetL = wetR = 0.f;
                }
                else
                {
                    wetL = readRing (ringL_, readPos_);
                    wetR = readRing (ringR_, readPos_);
                    readPos_ += 1.0 - e / dur;
                }
                break;
            }
            case GlitchPad::PitchDive:
            {
                wetL = readRing (ringL_, readPos_);
                wetR = readRing (ringR_, readPos_);
                readPos_ += 1.0 - 0.5 * std::min (1.0, e / (4.0 * spb));
                if (static_cast<double> (t) - readPos_ > 3.5 * spb)
                    readPos_ += spb;
                break;
            }
            case GlitchPad::Crush:
            {
                if (holdCount_ == 0)
                {
                    heldL_ = std::round (inL * 32.f) / 32.f;
                    heldR_ = std::round (inR * 32.f) / 32.f;
                }
                holdCount_ = (holdCount_ + 1) % 8;
                wetL = heldL_;
                wetR = heldR_;
                break;
            }
            case GlitchPad::Gate:
            {
                const double l32 = spb / 8.0;
                const double ph = std::fmod (e, l32) / l32;
                if (ph >= 0.5)
                    wetL = wetR = 0.f;
                break;
            }
        }

        if (releasing_)
            mix_ = std::max (0.f, mix_ - kFadeStep);
        else
            mix_ = std::min (1.f, mix_ + kFadeStep);

        if (mix_ >= 1.f)
        {
            left[i] = wetL;
            right[i] = wetR;
        }
        else
        {
            left[i] = inL * (1.f - mix_) + wetL * mix_;
            right[i] = inR * (1.f - mix_) + wetR * mix_;
        }

        if (releasing_ && mix_ <= 0.f)
        {
            active_ = -1;
            releasing_ = false;
        }
    }

    // keep down events whose start is still in the future (they are already pending), drop the rest
    numEvents_ = 0;
}

} // namespace jackal

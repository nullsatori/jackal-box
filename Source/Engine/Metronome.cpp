#include "Metronome.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace jackal
{

namespace
{
    void synthesizeClick (std::vector<float>& buffer, double sampleRate, double frequency)
    {
        const auto length = static_cast<std::size_t> (std::lround (0.020 * sampleRate));
        const auto attack = std::max<std::size_t> (1, static_cast<std::size_t> (std::lround (0.0005 * sampleRate)));
        const double decayLn = std::log (0.001); // -60 dB at the end

        buffer.assign (length, 0.f);

        for (std::size_t i = 0; i < length; ++i)
        {
            double env = 1.0;

            // attack starts at i + 1 so the very first click sample is already non-zero
            if (i < attack)
                env = 0.5 * (1.0 - std::cos (std::numbers::pi * static_cast<double> (i + 1) / static_cast<double> (attack + 1)));
            else if (length > attack)
                env = std::exp (decayLn * static_cast<double> (i - attack) / static_cast<double> (length - attack));

            // cosine phase: non-zero at i = 0, so the onset lands exactly on the beat sample
            const double osc = std::cos (2.0 * std::numbers::pi * frequency * static_cast<double> (i) / sampleRate);
            buffer[i] = static_cast<float> (env * osc);
        }
    }
} // namespace

void Metronome::prepare (double sampleRate, int /*maxBlockSize*/)
{
    synthesizeClick (accentClick_, sampleRate, 1760.0);
    synthesizeClick (normalClick_, sampleRate, 880.0);
    reset();
}

void Metronome::reset() noexcept
{
    nextBeatIndex_ = 0;
    voiceBuffer_ = nullptr;
    voicePos_ = 0;
}

void Metronome::startVoice (bool accent) noexcept
{
    voiceBuffer_ = accent ? &accentClick_ : &normalClick_;
    voicePos_ = 0;
}

void Metronome::renderVoice (float* const* out, int numOut, int from, int to, float gain) noexcept
{
    if (voiceBuffer_ == nullptr)
        return;

    const auto& buffer = *voiceBuffer_;

    for (int i = from; i < to; ++i)
    {
        if (voicePos_ >= buffer.size())
        {
            voiceBuffer_ = nullptr;
            return;
        }

        const float v = buffer[voicePos_++] * gain;

        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
                out[ch][i] += v;
    }
}

void Metronome::process (const Transport& transport, int64_t blockStart,
                         float* const* out, int numOut, int numSamples, float gain) noexcept
{
    if (! transport.isPlaying())
    {
        reset();
        return;
    }

    const int64_t blockEnd = blockStart + numSamples;
    int cursor = 0;

    for (int64_t beatSample = transport.sampleOfBeat (nextBeatIndex_);
         beatSample < blockEnd;
         beatSample = transport.sampleOfBeat (nextBeatIndex_))
    {
        const int64_t offset = beatSample - blockStart;

        if (offset >= 0)
        {
            const int start = static_cast<int> (offset);
            renderVoice (out, numOut, cursor, start, gain);
            cursor = start;
            startVoice (nextBeatIndex_ % kBeatsPerBar == 0);
        }

        ++nextBeatIndex_;
    }

    renderVoice (out, numOut, cursor, numSamples, gain);
}

} // namespace jackal

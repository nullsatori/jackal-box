#pragma once

#include "Transport.h"

#include <cstdint>
#include <vector>

namespace jackal
{

// Click synthesized in prepare(): accent 1760 Hz, normal 880 Hz, 20 ms,
// 0.5 ms raised-cosine attack, exponential decay to -60 dB. One voice, retrigger cuts.
class Metronome
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    // Adds (+=) clicks into out for the block that starts at blockStart.
    void process (const Transport& transport, int64_t blockStart,
                  float* const* out, int numOut, int numSamples, float gain) noexcept;

private:
    void startVoice (bool accent) noexcept;
    void renderVoice (float* const* out, int numOut, int from, int to, float gain) noexcept;

    std::vector<float> accentClick_;
    std::vector<float> normalClick_;

    int64_t nextBeatIndex_ = 0;
    const std::vector<float>* voiceBuffer_ = nullptr;
    std::size_t voicePos_ = 0;
};

} // namespace jackal

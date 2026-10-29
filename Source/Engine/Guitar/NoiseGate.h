#pragma once

namespace jackal
{

// Mono noise gate before the amp: envelope follower with 6 dB hysteresis, smoothed gain.
class NoiseGate
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;   // starts open
    void process (float* mono, int n, float thresholdDb) noexcept;   // <= -80 dB: bit-exact passthrough

private:
    float aAtt_ = 0.f, aRel_ = 0.f, aGain_ = 0.f;
    float env_ = 1.f, gain_ = 1.f;
    bool open_ = true;
};

} // namespace jackal

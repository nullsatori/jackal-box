#pragma once

namespace jackal
{

// Copies the selected input channel into every output with a smoothed gain (one-pole, 5 ms).
class InputMonitor
{
public:
    void prepare (double sampleRate) noexcept;
    void setInputChannel (int channel) noexcept;   // -1 = none; out of range = silence
    void process (const float* const* in, int numIn, float* const* out, int numOut,
                  int numSamples, float gainLinear, bool enabled) noexcept;

    int inputChannel() const noexcept { return channel_; }

private:
    double smoothingCoeff_ = 0.0;
    float currentGain_ = 0.f;
    bool gainInitialised_ = false;
    int channel_ = 0;
};

} // namespace jackal

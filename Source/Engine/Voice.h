#pragma once

#include "Sample.h"

#include <cstdint>

namespace jackal
{

// Everything a voice needs at the moment of a hit; assembled by the sequencer.
struct TriggerParams
{
    const Sample* sample = nullptr;
    int64_t startSample = 0, endSample = 0;   // end exclusive
    bool reverse = false;
    float gain = 1.f;                         // velocity / 127 x ratchet ramp
    float pitchSemitones = 0.f;
    float decayMs = 5000.f, toneHz = 20000.f, pan = 0.f, volumeDb = 0.f;
    double rateFit = 1.0;
    int attackSamples = 32;                   // linear attack (i + 1) / attack; 0 = none
    float shakalMacro = -1.f;                 // step lock of the SHAKAL macro, -1 = none (stage 3)
};

class Voice
{
public:
    void prepare (double sampleRate) noexcept;
    void start (const TriggerParams&) noexcept;
    void release (int fadeSamples) noexcept;   // linear fade to zero, then stop
    void kill() noexcept;
    bool active() const noexcept { return active_; }
    bool releasing() const noexcept { return fadeTotal_ > 0; }
    const Sample* sample() const noexcept { return active_ ? p_.sample : nullptr; }
    void render (float* left, float* right, int from, int to) noexcept;   // += into the bus

private:
    double engineRate_ = 48000.0;
    TriggerParams p_ {};
    bool active_ = false;
    double pos_ = 0.0, rate_ = 1.0;
    double env_ = 1.0, envMul_ = 1.0;
    bool useEnv_ = false;
    float toneA_ = 1.f, toneL_ = 0.f, toneR_ = 0.f;
    bool useTone_ = false;
    float gainL_ = 1.f, gainR_ = 1.f;
    int attackPos_ = 0;
    int fadeLeft_ = 0, fadeTotal_ = 0;
};

} // namespace jackal

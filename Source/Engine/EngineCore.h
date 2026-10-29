#pragma once

#include "CommandQueue.h"
#include "EngineTypes.h"
#include "InputMonitor.h"
#include "LevelMeter.h"
#include "Metronome.h"
#include "Transport.h"

#include <atomic>
#include <cstdint>

namespace jackal
{

// The only thing AudioEngine talks to. prepare() may allocate; process() never does.
class EngineCore
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numInputs, int numOutputs);
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept;

    CommandQueue& commands() noexcept { return commands_; }
    EngineParams& params() noexcept { return params_; }

    TransportState transportSnapshot() const noexcept;

    const LevelMeter& inputMeter() const noexcept { return inputMeter_; }
    const LevelMeter& outputMeter() const noexcept { return outputMeter_; }

private:
    void applyCommand (const Command& command) noexcept;
    void publishSnapshot() noexcept;

    CommandQueue commands_;
    EngineParams params_;

    Transport transport_;
    Metronome metronome_;
    InputMonitor monitor_;
    LevelMeter inputMeter_;
    LevelMeter outputMeter_;

    std::atomic<bool> snapPlaying_ { false };
    std::atomic<double> snapBpm_ { kDefaultBpm };
    std::atomic<int64_t> snapPositionSamples_ { 0 };
    std::atomic<double> snapPositionBeats_ { 0.0 };
};

} // namespace jackal

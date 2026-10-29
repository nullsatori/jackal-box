#include "EngineCore.h"

#include <cmath>

namespace jackal
{

namespace
{
    float dbToGain (float db) noexcept
    {
        return db <= -60.f ? 0.f : std::pow (10.f, db / 20.f);
    }
} // namespace

void EngineCore::prepare (double sampleRate, int maxBlockSize, int /*numInputs*/, int /*numOutputs*/)
{
    transport_.prepare (sampleRate);
    metronome_.prepare (sampleRate, maxBlockSize);
    monitor_.prepare (sampleRate);
    inputMeter_.reset();
    outputMeter_.reset();
    publishSnapshot();
}

void EngineCore::applyCommand (const Command& command) noexcept
{
    switch (command.type)
    {
        case CommandType::Play:            transport_.play(); metronome_.reset(); break;
        case CommandType::Stop:            transport_.stop(); metronome_.reset(); break;
        case CommandType::SetBpm:          transport_.setBpm (static_cast<double> (command.value)); break;
        case CommandType::SetInputChannel: monitor_.setInputChannel (command.ivalue); break;
    }
}

void EngineCore::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept
{
    // (1) discrete commands, applied at the block boundary
    Command command;
    while (commands_.pop (command))
        applyCommand (command);

    // (2) continuous params, read once per block
    const float monitorGain = dbToGain (params_.monitorGainDb.load (std::memory_order_relaxed));
    const bool monitorEnabled = params_.monitorEnabled.load (std::memory_order_relaxed);
    const bool metronomeEnabled = params_.metronomeEnabled.load (std::memory_order_relaxed);
    const float metronomeGain = params_.metronomeGain.load (std::memory_order_relaxed);

    // (3) clear output
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            for (int i = 0; i < numSamples; ++i)
                out[ch][i] = 0.f;

    // (4) raw input meter (selected channel, before gain)
    const int inputChannel = monitor_.inputChannel();
    if (in != nullptr && inputChannel >= 0 && inputChannel < numIn && in[inputChannel] != nullptr)
        inputMeter_.update (in + inputChannel, 1, numSamples);
    else
        inputMeter_.update (nullptr, 0, numSamples);

    // (5) guitar monitor
    monitor_.process (in, numIn, out, numOut, numSamples, monitorGain, monitorEnabled);

    // (6) metronome
    if (transport_.isPlaying() && metronomeEnabled)
        metronome_.process (transport_, transport_.positionSamples(), out, numOut, numSamples, metronomeGain);

    // (7) master meter
    outputMeter_.update (out, numOut, numSamples);

    // (8) advance transport
    transport_.advance (numSamples);

    // (9) snapshot for UI
    publishSnapshot();
}

void EngineCore::publishSnapshot() noexcept
{
    const auto s = transport_.state();
    snapPlaying_.store (s.playing, std::memory_order_relaxed);
    snapBpm_.store (s.bpm, std::memory_order_relaxed);
    snapPositionSamples_.store (s.positionSamples, std::memory_order_relaxed);
    snapPositionBeats_.store (s.positionBeats, std::memory_order_relaxed);
}

TransportState EngineCore::transportSnapshot() const noexcept
{
    return { snapPlaying_.load (std::memory_order_relaxed),
             snapBpm_.load (std::memory_order_relaxed),
             snapPositionSamples_.load (std::memory_order_relaxed),
             snapPositionBeats_.load (std::memory_order_relaxed) };
}

} // namespace jackal

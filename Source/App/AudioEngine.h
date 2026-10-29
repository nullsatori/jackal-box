#pragma once

#include "Engine/EngineCore.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace jackal
{

struct LatencyInfo
{
    int inputSamples = 0;
    int outputSamples = 0;
    int bufferSamples = 0;
    double sampleRate = 0.0;
    int xrunCount = -1;
    bool deviceOpen = false;
};

// Owns the device manager and the engine core. The audio callback only forwards to EngineCore.
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // Message thread only.
    void initialise (const juce::XmlElement* savedState);

    juce::AudioDeviceManager& deviceManager() noexcept { return deviceManager_; }
    EngineCore& core() noexcept { return core_; }

    LatencyInfo latency() const;
    const juce::String& lastError() const noexcept { return lastError_; }
    bool inputOpen() const;
    juce::StringArray activeInputChannelNames() const;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager deviceManager_;
    EngineCore core_;
    juce::String lastError_;
};

} // namespace jackal

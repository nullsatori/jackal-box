#include "AudioEngine.h"

namespace jackal
{

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    deviceManager_.removeAudioCallback (this);
    deviceManager_.closeAudioDevice();
}

void AudioEngine::initialise (const juce::XmlElement* savedState)
{
    if (savedState != nullptr)
    {
        lastError_ = deviceManager_.initialise (2, 2, savedState, true);
    }
    else
    {
        lastError_ = deviceManager_.initialiseWithDefaultDevices (2, 2);

        // first launch: prefer ASIO when the build has it
        for (auto* type : deviceManager_.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == "ASIO")
            {
                deviceManager_.setCurrentAudioDeviceType ("ASIO", true);
                break;
            }
        }
    }

    if (! inputOpen())
    {
        const auto error = deviceManager_.initialiseWithDefaultDevices (0, 2);
        if (error.isNotEmpty())
            lastError_ = error;
    }

    deviceManager_.addAudioCallback (this);
}

bool AudioEngine::inputOpen() const
{
    auto* device = deviceManager_.getCurrentAudioDevice();
    return device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > 0;
}

juce::StringArray AudioEngine::activeInputChannelNames() const
{
    juce::StringArray result;

    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        const auto names = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();

        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                result.add (names[i]);
    }

    return result;
}

LatencyInfo AudioEngine::latency() const
{
    LatencyInfo info;

    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        info.deviceOpen = true;
        info.inputSamples = device->getInputLatencyInSamples();
        info.outputSamples = device->getOutputLatencyInSamples();
        info.bufferSamples = device->getCurrentBufferSizeSamples();
        info.sampleRate = device->getCurrentSampleRate();
        info.xrunCount = device->getXRunCount();
    }

    return info;
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                    float* const* outputChannelData, int numOutputChannels,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    core_.process (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    core_.prepare (device->getCurrentSampleRate(),
                   device->getCurrentBufferSizeSamples(),
                   device->getActiveInputChannels().countNumberOfSetBits(),
                   device->getActiveOutputChannels().countNumberOfSetBits());
}

void AudioEngine::audioDeviceStopped()
{
}

} // namespace jackal

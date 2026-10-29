#include "AudioEngine.h"

#include "AppSettings.h"

#include <chrono>
#include <cmath>

namespace jackal
{

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    stopAudio();
    deviceManager_.closeAudioDevice();
}

void AudioEngine::stopAudio()
{
    if (midiInput_ != nullptr)
    {
        midiInput_->stop();
        midiInput_ = nullptr;
    }
    if (callbackAdded_)
    {
        deviceManager_.removeAudioCallback (this);
        callbackAdded_ = false;
    }
}

void AudioEngine::openMidiInput (const juce::String& identifier)
{
    if (midiInput_ != nullptr)
    {
        midiInput_->stop();
        midiInput_ = nullptr;
    }

    if (identifier.isEmpty())
        return;

    midiInput_ = juce::MidiInput::openDevice (identifier, this);
    if (midiInput_ != nullptr)
        midiInput_->start();
}

juce::String AudioEngine::midiInputName() const
{
    return midiInput_ != nullptr ? midiInput_->getName() : juce::String();
}

juce::String AudioEngine::midiInputIdentifier() const
{
    return midiInput_ != nullptr ? midiInput_->getIdentifier() : juce::String();
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // MIDI thread: three bytes into the SPSC queue, nothing else (SysEx is dropped)
    if (message.isSysEx() || message.getRawDataSize() < 2 || message.getRawDataSize() > 3)
        return;

    const auto* raw = message.getRawData();
    MidiEvent e;
    e.status = raw[0];
    e.data1 = raw[1];
    e.data2 = message.getRawDataSize() > 2 ? raw[2] : uint8_t (0);
    core_.midi().push (e);
    uiMidi_.push (e);   // stage 5: the same event for MidiMapper (learn, CC, actions)
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
    callbackAdded_ = true;
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
    const auto t0 = std::chrono::steady_clock::now();
    core_.process (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
    const auto t1 = std::chrono::steady_clock::now();

    const double budget = numSamples / sampleRate_;
    if (budget > 0.0)
        core_.cpuLoad().store (static_cast<float> (std::chrono::duration<double> (t1 - t0).count() / budget), std::memory_order_relaxed);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate_ = device->getCurrentSampleRate() > 0.0 ? device->getCurrentSampleRate() : 48000.0;
    core_.prepare (device->getCurrentSampleRate(),
                   device->getCurrentBufferSizeSamples(),
                   device->getActiveInputChannels().countNumberOfSetBits(),
                   device->getActiveOutputChannels().countNumberOfSetBits());
}

juce::String AudioEngine::deviceName() const
{
    auto* d = deviceManager_.getCurrentAudioDevice();
    return d != nullptr ? d->getTypeName() + "-" + d->getName() : juce::String();
}

void AudioEngine::updateLoopComp (const AppSettings& settings)
{
    const auto info = latency();
    const double sr = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    const int measured = settings.loopCal (deviceName());
    const int base = settings.loopUseMeasured() && measured >= 0 ? measured : info.inputSamples + info.outputSamples;
    const auto manual = static_cast<int> (std::llround (settings.loopManualMs() * sr / 1000.0));
    core_.params().loopCompSamples.store (base + manual, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceStopped()
{
}

} // namespace jackal

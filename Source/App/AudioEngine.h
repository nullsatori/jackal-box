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
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::MidiInputCallback
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

    // MIDI: one device at a time, so the MIDI queue keeps a single writer.
    void openMidiInput (const juce::String& identifier);   // empty = none
    juce::String midiInputName() const;
    juce::String midiInputIdentifier() const;
    // Stage 5: a second copy of every MIDI event for the message thread (MidiMapper drains it at 60 Hz).
    SpscQueue<MidiEvent, 1024>& uiMidi() noexcept { return uiMidi_; }

    // Stage 4: loopCompSamples = (measured round trip for this device, or driver in + out) + manual offset.
    // Message thread: at start, on device change, after a calibration and when the slider moves.
    void updateLoopComp (const class AppSettings& settings);
    juce::String deviceName() const;

    // Removes the audio callback and closes MIDI. After this the engine core is not running.
    void stopAudio();

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    juce::AudioDeviceManager deviceManager_;
    EngineCore core_;
    SpscQueue<MidiEvent, 1024> uiMidi_;
    juce::String lastError_;
    std::unique_ptr<juce::MidiInput> midiInput_;
    double sampleRate_ = 48000.0;
    bool callbackAdded_ = false;
};

} // namespace jackal

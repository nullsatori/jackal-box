#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"
#include "DeviceSettingsPanel.h"
#include "MonitorPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

// Audio device, guitar monitor and MIDI input, drawn over the right side of the window.
class SettingsOverlay : public juce::Component
{
public:
    SettingsOverlay (AudioEngine& engine, AppSettings& settings);

    void refreshMidiDevices();
    void refreshChannels() { monitor.refreshChannels(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AudioEngine& engine_;
    AppSettings& settings_;
    DeviceSettingsPanel devices;
    MonitorPanel monitor;
    juce::Label midiLabel { {}, "MIDI input" };
    juce::ComboBox midi;
    juce::Array<juce::MidiDeviceInfo> midiDevices_;
};

} // namespace jackal

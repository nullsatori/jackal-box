#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"
#include "DeviceSettingsPanel.h"
#include "MonitorPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

// Audio device, guitar monitor and MIDI input, drawn over the right side of the window.
class SettingsOverlay : public juce::Component, private juce::Timer
{
public:
    SettingsOverlay (AudioEngine& engine, AppSettings& settings);

    bool calibrating() const;
    void cancelCalibration();
    void refreshTiming();

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

    // stage 4: loop timing and VST3 folders
    void timerCallback() override;
    void startCalibration();
    juce::Label timingTitle { {}, "LOOP TIMING" }, driverLine, manualLabel { {}, "MANUAL" }, measuredLine;
    juce::Slider manual { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton cal { "CAL" };
    juce::ToggleButton useMeasured { "USE MEASURED" };
    juce::Label foldersTitle { {}, "VST3 FOLDERS" };
    juce::ComboBox folders;
    juce::TextButton addFolder { "ADD..." }, removeFolder { "REMOVE" };
    std::unique_ptr<juce::FileChooser> chooser_;
    CalState lastCal_ = CalState::Idle;
};

} // namespace jackal

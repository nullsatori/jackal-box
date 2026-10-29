#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"
#include "DeviceSettingsPanel.h"
#include "MidiMapper.h"
#include "MonitorPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace jackal
{

// Audio device, guitar monitor and MIDI input, drawn over the right side of the window.
// Stage 5: two pages, AUDIO and MIDI (channel, default notes, learn, bindings, TRACK HIT learn buttons).
class SettingsOverlay : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    SettingsOverlay (AudioEngine& engine, AppSettings& settings, MidiMapper& mapper);
    void showPage (int page);   // 0 AUDIO, 1 MIDI

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

    // stage 5: MIDI page
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void refreshMidiMap();
    bool onAudioPage() const noexcept { return page_ == 0; }

    MidiMapper& mapper_;
    int page_ = 0;
    juce::TextButton audioTab { "AUDIO" }, midiTab { "MIDI" };
    juce::Label midiTitle { {}, "MIDI" }, channelLabel { {}, "CHANNEL" }, bindingsTitle { {}, "BINDINGS" }, hitLabel { {}, "+ TRACK HIT" },
                midiHint;
    juce::ComboBox channel;
    juce::ToggleButton defaultNotes { "NOTES C..G -> TRACKS" };
    juce::TextButton learn { "LEARN" }, clearAll { "CLEAR ALL" };
    std::array<juce::TextButton, 8> hitButtons;
    juce::ListBox bindings { "bindings", this };
    juce::uint32 clearArmedMs_ = 0;
    uint32_t shownMapVersion_ = 0;
};

} // namespace jackal

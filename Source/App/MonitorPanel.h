#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

class MonitorPanel : public juce::Component, private juce::Timer
{
public:
    MonitorPanel (AudioEngine& engine, AppSettings& settings);

    void refreshChannels(); // call when the device changes
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    AudioEngine& engine;
    AppSettings& settings;

    juce::ToggleButton monitorToggle { "Monitor" };
    juce::Label gainLabel { {}, "Gain" };
    juce::Slider gain { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label channelLabel { {}, "Input" };
    juce::ComboBox channel;
    juce::Label status;

    juce::Rectangle<int> meterArea;
    float shownPeakDb = -60.f;
    float shownRmsDb = -60.f;
    float holdPeakDb = -60.f;
    juce::uint32 holdSince = 0;
};

} // namespace jackal

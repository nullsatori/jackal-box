#pragma once

#include "AppSettings.h"
#include "Engine/EngineCore.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

class TransportBar : public juce::Component, private juce::Timer
{
public:
    TransportBar (EngineCore& core, AppSettings& settings);

    void togglePlay();
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;
    void pushBpm (double bpm);

    EngineCore& core;
    AppSettings& settings;

    juce::TextButton playButton { "Play" };
    juce::Label bpmLabel { {}, "BPM" };
    juce::Slider bpmSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton clickToggle { "Click" };
    juce::Slider clickGain { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label position { {}, "1.1" };

    bool shownPlaying = false;
};

} // namespace jackal

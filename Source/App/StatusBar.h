#pragma once

#include "AudioEngine.h"
#include "LatencyDisplay.h"
#include "ProjectController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jackal
{

class StatusBar : public juce::Component, private juce::Timer
{
public:
    StatusBar (ProjectController& controller, AudioEngine& engine);

    std::function<void()> onSettings;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ProjectController& c_;
    AudioEngine& engine_;
    LatencyDisplay latency;
    juce::TextButton settingsButton { "AUDIO/MIDI" };
    float inLevel_ = 0.f, outLevel_ = 0.f;
    juce::Rectangle<int> meterArea_, infoArea_;
};

} // namespace jackal

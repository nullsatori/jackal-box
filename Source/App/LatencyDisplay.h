#pragma once

#include "AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

// Driver-reported latency. Loopback calibration comes in stage 4.
class LatencyDisplay : public juce::Component, private juce::Timer
{
public:
    explicit LatencyDisplay (AudioEngine& engine);
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    AudioEngine& engine;
    juce::String text;
    bool isError = false;
};

} // namespace jackal

#pragma once

#include "ProjectController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

// Waveform of the selected track's sample with slice boundaries (or the Start/End window).
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (ProjectController& controller) : c_ (controller) {}
    void paint (juce::Graphics&) override;

private:
    ProjectController& c_;
};

} // namespace jackal

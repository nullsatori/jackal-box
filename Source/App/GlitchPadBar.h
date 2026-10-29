#pragma once

#include "MidiTargets.h"
#include "ProjectController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

// Eight master glitch pads. Mouse press = hold. Ctrl+click clears that pad from the glitch lane.
class GlitchPadBar : public juce::Component, public MidiTargetArea, private juce::Timer
{
public:
    explicit GlitchPadBar (ProjectController& controller);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static const char* padName (int pad);
    juce::String midiTargetAt (juce::Point<int> p) const override;

private:
    void timerCallback() override;
    juce::Rectangle<int> padBounds (int pad) const;
    int padAt (juce::Point<int>) const;

    ProjectController& c_;
    int held_ = -1;
    int shownActive_ = -2, shownPending_ = -2, shownLane_ = -2;
    bool blink_ = false;
    int tick_ = 0;
    uint32_t shownVersion_ = 0;
};

} // namespace jackal

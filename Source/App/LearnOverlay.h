#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>

namespace jackal
{

// Transparent component over the whole main window. It takes a mouse press only
//  - in learn mode (pick a control), or
//  - for a right click on a control that has a midiTarget (the "MIDI learn / Clear MIDI" menu);
// everything else falls through to the controls below.
class LearnOverlay : public juce::Component
{
public:
    LearnOverlay();

    void setPicking (bool on);
    bool picking() const noexcept { return picking_; }

    // comp: the control (or the painted area's component); target: its midiTarget string (empty: nothing mappable)
    std::function<void (juce::Component* comp, const juce::String& target)> onPick;
    std::function<void (juce::Component* comp, const juce::String& target)> onContextMenu;

    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void paint (juce::Graphics&) override;

    // the control (or a parent) with a midiTarget, or a MidiTargetArea that has one at this point
    std::pair<juce::Component*, juce::String> targetAt (juce::Point<int> local);

private:
    bool picking_ = false;
    bool inLookup_ = false;
};

} // namespace jackal

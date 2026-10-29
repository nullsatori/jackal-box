#pragma once

#include "MidiTargets.h"
#include "ProjectController.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

// A..H (click = queue, drag onto another = copy), x2, grid pages, MUTATE + strength, UNDO/REDO.
// Stage 5: the playing pattern (nixie outline) and the edited one (cyan fill) are separate; in SONG mode a click
// only picks the pattern to edit; Shift+click appends it to the song.
class PatternStrip : public juce::Component, public MidiTargetArea, private juce::Timer
{
public:
    explicit PatternStrip (ProjectController& controller);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void copySelected() { clipboard_ = c_.editPatternIndex(); }
    void pasteSelected();
    juce::String midiTargetAt (juce::Point<int> p) const override;

private:
    void timerCallback() override;
    juce::Rectangle<int> patternButton (int i) const;
    int patternAt (juce::Point<int>) const;
    void requestCopy (int from, int to);

    ProjectController& c_;
    juce::TextButton doubleButton { "x2" }, mutateButton { "MUTATE" }, undoButton { "UNDO" }, redoButton { "REDO" };
    std::array<juce::TextButton, 4> pageButtons;
    Knob strength;
    int dragFrom_ = -1;
    int clipboard_ = -1;
    int pendingCopyFrom_ = -1, pendingCopyTo_ = -1;
    juce::uint32 pendingCopyTime_ = 0;
    uint32_t shownVersion_ = 0;
    int shownPlaying_ = -2;
    bool blink_ = false;
    int blinkTick_ = 0;
};

} // namespace jackal

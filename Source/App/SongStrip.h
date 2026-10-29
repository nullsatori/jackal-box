#pragma once

#include "ProjectController.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace jackal
{

// The song row under the pattern strip (contract stage 5, §9.2): PTN | SONG, FOLLOW, LOOP, 32 cells, CLEAR, counter.
class SongStrip : public juce::Component, private juce::Timer
{
public:
    explicit SongStrip (ProjectController& controller);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    std::optional<int> hoverEntry() const;   // Delete removes it (MainComponent)

private:
    void timerCallback() override;
    juce::Rectangle<int> cell (int i) const;
    int cellAt (juce::Point<int>) const;      // 0..31 or -1
    int dropIndexAt (juce::Point<int>) const; // insertion point for a drag, 0..length
    juce::String signature() const;

    ProjectController& c_;
    juce::TextButton ptnButton { "PTN" }, songButton { "SONG" }, followButton { "FOLLOW" }, loopButton { "LOOP" }, clearButton { "CLEAR" };
    juce::Rectangle<int> cellsArea_, counterArea_;
    int cellSize_ = 28;
    int pressCell_ = -1, hoverCell_ = -1, dropAt_ = -1;
    bool dragging_ = false;
    bool blink_ = false;
    int tick_ = 0;
    juce::String shown_;
};

} // namespace jackal

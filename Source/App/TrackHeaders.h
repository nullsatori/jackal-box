#pragma once

#include "MidiTargets.h"
#include "ProjectController.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace jackal
{

// One row per track: name, M, S, volume, SHAKAL macro (orange ring while a p-lock plays), hit LED.
class TrackHeaders : public juce::Component, private juce::Timer
{
public:
    explicit TrackHeaders (ProjectController& controller);

    void paint (juce::Graphics&) override;
    void resized() override;
    int rowAt (juce::Point<int> p) const;

private:
    struct Row : juce::Component, MidiTargetArea
    {
        Row (ProjectController& c, int track);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void refresh();
        juce::String midiTargetAt (juce::Point<int> p) const override;

        ProjectController& c;
        int track;
        juce::TextButton mute { "M" }, solo { "S" };
        Knob volume, shakal;
        float led = 0.f;
        bool refreshing = false;
    };

    void timerCallback() override;

    ProjectController& c_;
    std::array<std::unique_ptr<Row>, kNumTracks> rows_;
    uint32_t shownVersion_ = 0;
};

} // namespace jackal

#pragma once

#include "ProjectController.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace jackal
{

// The big SHAKAL macro knob with its scale, plus the six effects of the chain (manual layer).
// One instance follows the selected track (FX tab), others the master (MST) and the guitar (GTR).
class ShakalSection : public juce::Component, private juce::Timer
{
public:
    // fixedChain: -1 follows the selected track, kMasterChain or kGuitarChain otherwise
    ShakalSection (ProjectController& controller, int fixedChain);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    // Macro knob: wheel = 2 % per click, double click = 0.
    struct MacroKnob : Knob
    {
        using Knob::Knob;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
        ProjectController* ctl = nullptr;
    };

    struct Row
    {
        FxSlot slot;
        juce::String title;
        FxParam onParam;
        std::vector<FxParam> knobParams;
        FxParam switchParam = FxParam::Count;
        juce::String switchOff, switchOn;

        juce::TextButton on { "ON" };
        juce::Label name;
        std::vector<std::unique_ptr<Knob>> knobs;
        std::vector<std::unique_ptr<juce::Label>> knobLabels;
        juce::TextButton sw;
        juce::Rectangle<int> led;
    };

    void timerCallback() override;
    int chain() const;
    void setupRow (Row& row);

    ProjectController& c_;
    const int fixed_;
    bool fixed() const noexcept { return fixed_ >= 0; }
    juce::Label title;
    std::array<juce::TextButton, 3> presets { juce::TextButton ("IRON"), juce::TextButton ("NET"), juce::TextButton ("MEM") };
    juce::TextButton bypass { "BYP" }, reroll { "RR" };
    MacroKnob macro;
    juce::Label macroValue;
    std::array<std::unique_ptr<Row>, 6> rows;

    uint32_t shownVersion_ = 0;
    uint8_t shownEngaged_ = 0xFF;
    bool refreshing_ = false;
};

} // namespace jackal

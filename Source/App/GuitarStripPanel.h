#pragma once

#include "AppSettings.h"
#include "ProjectController.h"
#include "Widgets.h"
#include "Plugins/InsertSlotButton.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jackal
{

// The guitar strip under the grid (1280 x 96): input, gate, low cut, two slots, SHAKAL, the loop of the
// edit pattern with REC / DUB / UNDO, bars, tap point, loop level and "to track".
class GuitarStripPanel : public juce::Component, private juce::Timer
{
public:
    GuitarStripPanel (ProjectController& c, AudioEngine& engine, AppSettings& settings);

    std::function<void (int chain, int slot)> onBrowse;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct LoopView : juce::Component
    {
        explicit LoopView (GuitarStripPanel& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        GuitarStripPanel& owner;
    };

    void timerCallback() override;
    void refresh();
    void setupKnob (Knob& k, juce::Label& l, const char* label, double lo, double hi, double def);

    ProjectController& c_;
    AudioEngine& engine_;
    AppSettings& settings_;
    Knob gain, gate, hpf, shakal, loopGain;
    juce::Label gainL, gateL, hpfL, shakalL, loopL, presetLetter;
    InsertSlotButton slot1, slot2;
    LoopView loopView { *this };
    juce::TextButton rec { "REC" }, dub { "DUB" }, undo { "UNDO" }, toTrack { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92TRK")) };
    juce::ComboBox bars;
    juce::TextButton tap { "AMP" };
    juce::Rectangle<int> meter_;
    float level_ = 0.f;
    uint32_t shownVersion_ = 0;
    bool refreshing_ = false;
    LoopSnapshot snap_ {};
};

} // namespace jackal

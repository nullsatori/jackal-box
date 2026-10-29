#pragma once

#include "ProjectController.h"
#include "ShakalSection.h"
#include "WaveformView.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace jackal
{

// Right panel with tabs: SRC (sample, slicing, knobs), FX (SHAKAL of the selected track),
// STEP (inspector of the last touched step), MST (master SHAKAL and gain).
class TrackPanel : public juce::Component, private juce::Timer
{
public:
    explicit TrackPanel (ProjectController& controller);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void openFileChooser();
    void editStep (const std::function<void (Step&)>& fn);
    void showTab (int tab);
    void layoutSrc (juce::Rectangle<int>);
    void layoutStep (juce::Rectangle<int>);
    std::vector<juce::Component*> srcComponents();
    std::vector<juce::Component*> stepComponents();

    std::array<juce::TextButton, 4> tabs { juce::TextButton ("SRC"), juce::TextButton ("FX"), juce::TextButton ("STEP"), juce::TextButton ("MST") };
    int shownTab_ = -1;

    ProjectController& c_;
    juce::TextEditor name;
    WaveformView wave;
    juce::TextButton load { "LOAD..." };
    juce::ComboBox slice, bars, choke;
    juce::ToggleButton fit { "FIT" };
    std::array<std::unique_ptr<Knob>, 7> knobs;
    std::array<juce::Label, 7> knobLabels;

    juce::Label stepTitle;
    Field velocity, ratchet, velRamp, pitchRamp, pitch, sliceField, probability, nudge;
    std::array<juce::Label, 8> fieldLabels;
    juce::ToggleButton reverse { "REV" };
    juce::ComboBox condition;
    std::array<juce::TextButton, kMaxPlocksPerStep> plockButtons;

    ShakalSection fx, mst;
    Knob masterGain;
    juce::Label masterGainLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    uint32_t shownVersion_ = 0;
    bool refreshing_ = false;
};

} // namespace jackal

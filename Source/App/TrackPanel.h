#pragma once

#include "ProjectController.h"
#include "WaveformView.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace jackal
{

// The selected track: sample, slicing, knobs, and the step inspector (last touched step).
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

    std::unique_ptr<juce::FileChooser> chooser;
    uint32_t shownVersion_ = 0;
    bool refreshing_ = false;
};

} // namespace jackal

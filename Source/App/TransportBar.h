#pragma once

#include "AppSettings.h"
#include "ProjectController.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

class TransportBar : public juce::Component, private juce::Timer
{
public:
    TransportBar (ProjectController& controller, EngineCore& core, AppSettings& settings);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    struct FillButton : juce::TextButton
    {
        explicit FillButton (ProjectController& c) : juce::TextButton ("FILL"), controller (c) {}
        void mouseDown (const juce::MouseEvent& e) override { juce::TextButton::mouseDown (e); controller.setFill (true); }
        void mouseUp (const juce::MouseEvent& e) override { juce::TextButton::mouseUp (e); controller.setFill (false); }
        ProjectController& controller;
    };

    void timerCallback() override;

    ProjectController& c_;
    EngineCore& core;
    AppSettings& settings;

    juce::TextButton playButton { "PLAY" }, recButton { "REC" };
    FillButton fillButton;
    juce::Label position, patternLetter, cpu, songInfo;   // songInfo: SONG 03/12 x2 1/2, END (stage 5)
    juce::Slider bpm { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton clickToggle { "CLICK" };
    juce::Slider clickGain { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    struct ClickLabel : juce::Label
    {
        std::function<void()> onClick;
        void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
    };

    Knob swing, master, masterShakal;
    juce::Label swingLabel;
    ClickLabel masterLabel, shakalLabel;

    bool shownPlaying = false;
    int blinkTick_ = 0;
    uint32_t shownVersion_ = 0;
};

} // namespace jackal

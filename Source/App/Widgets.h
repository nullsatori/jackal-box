#pragma once

#include "JackalLookAndFeel.h"
#include "ProjectController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jackal
{

// Rotary knob that opens an undo transaction when a gesture starts (drag or wheel),
// so one drag is one undo step.
class Knob : public juce::Slider
{
public:
    Knob (ProjectController& c, juce::String name) : controller (c), gestureName (std::move (name))
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setMouseClickGrabsKeyboardFocus (false);
        setWantsKeyboardFocus (false);
        onDragStart = [this] { controller.beginGesture (gestureName); };
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        controller.beginGesture (gestureName);
        juce::Slider::mouseWheelMove (e, w);
    }

    void setLocked (bool locked)
    {
        if (static_cast<bool> (getProperties()[JackalLookAndFeel::lockedProperty]) != locked)
        {
            getProperties().set (JackalLookAndFeel::lockedProperty, locked);
            repaint();
        }
    }

private:
    ProjectController& controller;
    juce::String gestureName;
};

// Small labelled numeric field for the step inspector (drag or wheel to change).
class Field : public juce::Slider
{
public:
    Field (ProjectController& c, juce::String name) : controller (c), gestureName (std::move (name))
    {
        setSliderStyle (juce::Slider::LinearBar);
        setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 18);
        setMouseClickGrabsKeyboardFocus (false);
        setWantsKeyboardFocus (false);
        setColour (juce::Slider::trackColourId, palette::neonCyan.withAlpha (0.25f));
        onDragStart = [this] { controller.beginGesture (gestureName); };
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        controller.beginGesture (gestureName);
        juce::Slider::mouseWheelMove (e, w);
    }

private:
    ProjectController& controller;
    juce::String gestureName;
};

inline void styleButton (juce::Button& b)
{
    b.setMouseClickGrabsKeyboardFocus (false);
    b.setWantsKeyboardFocus (false);
}

} // namespace jackal

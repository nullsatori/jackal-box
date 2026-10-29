#pragma once

#include "Model/MidiMap.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

// UI components mark themselves as MIDI targets with this property (an address of §8.4 or "action:<name>[:<i>]").
inline constexpr const char* kMidiTargetProperty = "midiTarget";
inline constexpr const char* kMidiMappedProperty = "midiMapped";   // "CC 21" while the target is bound
inline constexpr const char* kLearningProperty = "learning";       // true while learn waits for a source

inline void setMidiTarget (juce::Component& c, const MidiTarget& t)
{
    const juce::String s (targetToString (t));
    if (c.getProperties()[kMidiTargetProperty].toString() != s)
        c.getProperties().set (kMidiTargetProperty, s);
}

// A component that paints several targets itself (A..H, glitch pads, track names) answers per point.
struct MidiTargetArea
{
    virtual ~MidiTargetArea() = default;
    virtual juce::String midiTargetAt (juce::Point<int> local) const = 0;   // empty: nothing there
};

namespace midifeedback
{
    juce::String mappedSource (const juce::String& target);   // "CC 21" or empty (MidiMapper.cpp)
    bool isLearning (const juce::String& target);
    // the same marks the look and feel draws on knobs and buttons: a 3 px dim dot, a magenta outline while learning
    void drawMarks (juce::Graphics&, juce::Rectangle<float> area, const juce::String& target);
} // namespace midifeedback

} // namespace jackal

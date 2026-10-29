#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

namespace palette
{
    const juce::Colour background { 0xFF0B0E11 };
    const juce::Colour panel      { 0xFF151A1F };
    const juce::Colour neonCyan   { 0xFF2EE6D6 };
    const juce::Colour nixie      { 0xFFFF8A2A };
    const juce::Colour text       { 0xFFC9D1D9 };
    const juce::Colour alarm      { 0xFFFF3B3B };
} // namespace palette

// Placeholder style: palette + monospace font. The real art pass comes later.
class JackalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    JackalLookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override;
};

} // namespace jackal
